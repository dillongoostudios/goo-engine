
// http://iquilezles.org/www/articles/smin/smin.htm
float smin( float a, float b, float k )
{
    float h = max(k-abs(a-b),0.0f);
    return min(a, b) - h*h*0.25f/k;
}

// http://iquilezles.org/www/articles/smin/smin.htm
float smax( float a, float b, float k )
{
    float h = max(k-abs(a-b),0.0f);
    return max(a, b) + h*h*0.25f/k;
}

// https://iquilezles.org/www/articles/fbmsdf/fbmsdf.htm
float sph( float3 i, float3 f, float3 c )
{
    // random radius at grid vertex i+c (please replace this hash by
    // something better if you plan to use this for a real application)
    float3  p = 17.0f*fract( (i+c)*0.3183099f+float3(0.11f,0.17f,0.13f) );
    float w = fract( p.x*p.y*p.z*(p.x+p.y+p.z) );
    float r = 0.7f*w*w;
    // distance to sphere at grid vertex i+c
    return length(f-c) - r;
}

// https://iquilezles.org/www/articles/fbmsdf/fbmsdf.htm
float sdBase( in float3 p )
{
    float3 i = floor(p);
    float3 f = fract(p);
    return min(min(min(sph(i,f,float3(0,0,0)),
                       sph(i,f,float3(0,0,1))),
                   min(sph(i,f,float3(0,1,0)),
                       sph(i,f,float3(0,1,1)))),
               min(min(sph(i,f,float3(1,0,0)),
                       sph(i,f,float3(1,0,1))),
                   min(sph(i,f,float3(1,1,0)),
                       sph(i,f,float3(1,1,1)))));
}

float sdFbm(
    in float3 p,
    in float detail,
    in float rough,
    in float inflate,
    in float smooth_fac,
    in float d )
{
    float s = 1.0f;
    for( int i=0; i < min(int(detail), 12); i++ )
    {
        // evaluate new octave
        float n = s*sdBase(p);

        // add
        n = smax(n,d - inflate*s, smooth_fac*s );
        d = smin(n,d      , smooth_fac*s );

        // prepare next octave
        p = mat3( 0.00f, 1.60f, 1.20f,
                  -1.60f, 0.72f,-0.96f,
                  -1.20f,-0.96f, 1.28f )*p;
        s = rough*s;
    }
    return d;
}

void node_sdf_noise(in float3 pos,
    in float dist_in,
    in float detail,
    in float rough,
    in float inflate_fac,
    in float smooth_fac,
    out float dist_out)
{
    dist_out = sdFbm(pos, detail, rough, inflate_fac, smooth_fac, dist_in);
}
