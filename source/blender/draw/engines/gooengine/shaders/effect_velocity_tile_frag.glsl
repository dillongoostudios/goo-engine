/* SPDX-FileCopyrightText: 2020-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Shaders that down-sample velocity buffer,
 *
 * Based on:
 * A Fast and Stable Feature-Aware Motion Blur Filter
 * by Jean-Philippe Guertin, Morgan McGuire, Derek Nowrouzezahrai
 *
 * Adapted from G3D Innovation Engine implementation.
 */

float4 sample_velocity(int2 texel)
{
  texel = clamp(texel, int2(0), velocityBufferSize - 1);
  float4 data = texelFetch(velocityBuffer, texel, 0);
  /* Decode data. */
  return (data * 2.0f - 1.0f) * viewportSize.xyxy;
}

float4 encode_velocity(float4 velocity)
{
  return velocity * viewportSizeInv.xyxy * 0.5f + 0.5f;
}

#ifdef TILE_GATHER

void main()
{
  float4 max_motion = float4(0.0f);
  float max_motion_len_sqr_prev = 0.0f;
  float max_motion_len_sqr_next = 0.0f;
  int2 texel = int2(gl_FragCoord.xy);
  texel = texel * gatherStep.yx + texel * EEVEE_VELOCITY_TILE_SIZE * gatherStep;

  for (int i = 0; i < EEVEE_VELOCITY_TILE_SIZE; ++i) {
    float4 motion = sample_velocity(texel + i * gatherStep);
    float motion_len_sqr_prev = dot(motion.xy, motion.xy);
    float motion_len_sqr_next = dot(motion.zw, motion.zw);

    if (motion_len_sqr_prev > max_motion_len_sqr_prev) {
      max_motion_len_sqr_prev = motion_len_sqr_prev;
      max_motion.xy = motion.xy;
    }
    if (motion_len_sqr_next > max_motion_len_sqr_next) {
      max_motion_len_sqr_next = motion_len_sqr_next;
      max_motion.zw = motion.zw;
    }
  }

  tileMaxVelocity = encode_velocity(max_motion);
}

#else /* TILE_EXPANSION */

bool neighbor_affect_this_tile(int2 offset, float2 velocity)
{
  /* Manhattan distance to the tiles, which is used for
   * differentiating corners versus middle blocks */
  float displacement = float(abs(offset.x) + abs(offset.y));
  /**
   * Relative sign on each axis of the offset compared
   * to the velocity for that tile.  In order for a tile
   * to affect the center tile, it must have a
   * neighborhood velocity in which x and y both have
   * identical or both have opposite signs relative to
   * offset. If the offset coordinate is zero then
   * velocity is irrelevant.
   */
  float2 point = sign(float2(offset) * velocity);

  float dist = (point.x + point.y);
  /**
   * Here's an example of the logic for this code.
   * In this diagram, the upper-left tile has offset = (-1, -1).
   * V1 is velocity = (1, -2). point in this case = (-1, 1), and therefore dist = 0,
   * so the upper-left tile does not affect the center.
   *
   * Now, look at another case. V2 = (-1, -2). point = (1, 1), so dist = 2 and the tile
   * does affect the center.
   *
   * V2(-1,-2)  V1(1, -2)
   *        \    /
   *         \  /
   *          \/___ ____ ____
   *  (-1, -1)|    |    |    |
   *          |____|____|____|
   *          |    |    |    |
   *          |____|____|____|
   *          |    |    |    |
   *          |____|____|____|
   */
  return (abs(dist) == displacement);
}

/**
 * Only gather neighborhood velocity into tiles that could be affected by it.
 * In the general case, only six of the eight neighbors contribute:
 *
 *  This tile can't possibly be affected by the center one
 *     |
 *     v
 *    ____ ____ ____
 *   |    | ///|/// |
 *   |____|////|//__|
 *   |    |////|/   |
 *   |___/|////|____|
 *   |  //|////|    | <--- This tile can't possibly be affected by the center one
 *   |_///|///_|____|
 */
void main()
{
  float4 max_motion = float4(0.0f);
  float max_motion_len_sqr_prev = -1.0f;
  float max_motion_len_sqr_next = -1.0f;

  int2 tile = int2(gl_FragCoord.xy);
  int2 offset = int2(0);
  for (offset.y = -1; offset.y <= 1; ++offset.y) {
    for (offset.x = -1; offset.x <= 1; ++offset.x) {
      float4 motion = sample_velocity(tile + offset);
      float motion_len_sqr_prev = dot(motion.xy, motion.xy);
      float motion_len_sqr_next = dot(motion.zw, motion.zw);

      if (motion_len_sqr_prev > max_motion_len_sqr_prev) {
        if (neighbor_affect_this_tile(offset, motion.xy)) {
          max_motion_len_sqr_prev = motion_len_sqr_prev;
          max_motion.xy = motion.xy;
        }
      }

      if (motion_len_sqr_next > max_motion_len_sqr_next) {
        if (neighbor_affect_this_tile(offset, motion.zw)) {
          max_motion_len_sqr_next = motion_len_sqr_next;
          max_motion.zw = motion.zw;
        }
      }
    }
  }

  tileMaxVelocity = encode_velocity(max_motion);
}

#endif
