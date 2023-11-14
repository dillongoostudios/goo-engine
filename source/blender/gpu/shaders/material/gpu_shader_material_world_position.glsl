
void world_position_get(out vec3 P)
{
  P = worldPosition;
}

void view_position_get(out vec3 P)
{
  P = viewPosition * vec3(1.0, 1.0, -1.0);
}
