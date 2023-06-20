
// http://iquilezles.org/www/articles/smin/smin.htm
float smin( float a, float b, float k )
{
    float h = max(k-abs(a-b),0.0);
    return min(a, b) - h*h*0.25/k;
}

// http://iquilezles.org/www/articles/smin/smin.htm
float smax( float a, float b, float k )
{
    float h = max(k-abs(a-b),0.0);
    return max(a, b) + h*h*0.25/k;
}

// https://iquilezles.org/www/articles/fbmsdf/fbmsdf.htm
float sph( vec3 i, vec3 f, vec3 c )
{
    // random radius at grid vertex i+c (please replace this hash by
    // something better if you plan to use this for a real application)
    vec3  p = 17.0*fract( (i+c)*0.3183099+vec3(0.11,0.17,0.13) );
    float w = fract( p.x*p.y*p.z*(p.x+p.y+p.z) );
    float r = 0.7*w*w;
    // distance to sphere at grid vertex i+c
    return length(f-c) - r;
}

// https://iquilezles.org/www/articles/fbmsdf/fbmsdf.htm
float sdBase( in vec3 p )
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    return min(min(min(sph(i,f,vec3(0,0,0)),
                       sph(i,f,vec3(0,0,1))),
                   min(sph(i,f,vec3(0,1,0)),
                       sph(i,f,vec3(0,1,1)))),
               min(min(sph(i,f,vec3(1,0,0)),
                       sph(i,f,vec3(1,0,1))),
                   min(sph(i,f,vec3(1,1,0)),
                       sph(i,f,vec3(1,1,1)))));
}

float sdFbm(
    in vec3 p,
    in float detail,
    in float rough,
    in float inflate,
    in float smooth_fac,
    in float d )
{
    float s = 1.0;
    for( int i=0; i < min(int(detail), 12); i++ )
    {
        // evaluate new octave
        float n = s*sdBase(p);

        // add
        n = smax(n,d - inflate*s, smooth_fac*s );
        d = smin(n,d      , smooth_fac*s );

        // prepare next octave
        p = mat3( 0.00, 1.60, 1.20,
                  -1.60, 0.72,-0.96,
                  -1.20,-0.96, 1.28 )*p;
        s = rough*s;
    }
    return d;
}

void node_sdf_noise(in vec3 pos,
    in float dist_in,
    in float detail,
    in float rough,
    in float inflate_fac,
    in float smooth_fac,
    out float dist_out)
{
    dist_out = sdFbm(pos, detail, rough, inflate_fac, smooth_fac, dist_in);
}
