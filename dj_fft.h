/* dj_fft.h - public domain FFT library
by Jonathan Dupuy

   INTERFACING

   define DJ_ASSERT(x) to avoid using assert.h.

   QUICK NOTES

*/

#ifndef DJ_INCLUDE_FFT_H
#define DJ_INCLUDE_FFT_H

#include <complex> // std::complex
#include <vector>  // std::vector

namespace dj {
namespace fft {
    // FFT argument: std::vector<std::complex>
    template <typename T> using arg = std::vector<std::complex<T>>;

    // FFT direction specifier
    enum class e_dir {DIR_FWD = +1, DIR_BWD = -1};

    // FFT routines
    template <typename T> arg<T> eval_1d(const arg<T> &xi, const e_dir &dir);
    template <typename T> arg<T> eval_2d(const arg<T> &xi, const e_dir &dir);
    template <typename T> arg<T> eval_3d(const arg<T> &xi, const e_dir &dir);

#ifdef DJ_FFT_ENABLE_GPU
    // GPU FFT routines (float precision only)
    arg<float> eval_1d_gpu(const arg<float> &xi, const e_dir &dir);
    arg<float> eval_2d_gpu(const arg<float> &xi, const e_dir &dir);
    arg<float> eval_3d_gpu(const arg<float> &xi, const e_dir &dir);
#endif
} // namespace fft
} // namespace dj

//
//
//// end header file ///////////////////////////////////////////////////////////


#include <cmath>
#include <cstdint>
#include <cstring> // std::memcpy

#ifndef DJ_ASSERT
#   include <cassert>
#   define DJ_ASSERT(x) assert(x)
#endif

namespace dj {
namespace fft {

constexpr auto Pi = 3.141592653589793238462643383279502884;

/*
 * Returns offset to most significant bit
 * NOTE: only works for positive power of 2s
 * examples:
 * 1b      -> 0d
 * 100b    -> 2d
 * 100000b -> 5d
 */
int findMSB(int x)
{
    DJ_ASSERT(x > 0 && "invalid input");
    int p = 0;

    while (x > 1) {
        x>>= 1;
        ++p;
    }

    return p;
}


/*
 *  Bit reverse an integer given a word of nb bits
 *  NOTE: Only works for 32-bit words max
 *  examples:
 *  10b      -> 01b
 *  101b     -> 101b
 *  1011b    -> 1101b
 *  0111001b -> 1001110b
 */
int bitr(uint32_t x, int nb)
{
    DJ_ASSERT(nb > 0 && 32 > nb && "invalid bit count");
    x = ( x               << 16) | ( x               >> 16);
    x = ((x & 0x00FF00FF) <<  8) | ((x & 0xFF00FF00) >>  8);
    x = ((x & 0x0F0F0F0F) <<  4) | ((x & 0xF0F0F0F0) >>  4);
    x = ((x & 0x33333333) <<  2) | ((x & 0xCCCCCCCC) >>  2);
    x = ((x & 0x55555555) <<  1) | ((x & 0xAAAAAAAA) >>  1);

    return ((x >> (32 - nb)) & (0xFFFFFFFF >> (32 - nb)));
}


/*
 * Computes a Fourier transform, i.e.,
 * xo[k] = 1/sqrt(N) sum(j=0 -> N-1) xi[j] exp(i 2pi j k / N)
 * with O(N log N) complexity using the butterfly technique
 *
 * NOTE: Only works for arrays whose size is a power-of-two
 */
template <typename T> arg<T> eval_1d(const arg<T> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt = (int)xi.size();
    int msb = findMSB(cnt);
    T nrm = T(1) / std::sqrt(cnt);
    arg<T> xo(cnt);

    // pre-process the input data
    for (int j = 0; j < cnt; ++j)
        xo[j] = nrm * xi[bitr(j, msb)];

    // fft passes
    for (int i = 0; i < msb; ++i) {
        int bm = 1 << i; // butterfly mask
        int bw = 2 << i; // butterfly width
        T ang = T(dir) * Pi / T(bm); // precomputation

        // fft butterflies
        for (int j = 0; j < (cnt/2); ++j) {
            int i1 = ((j >> i) << (i + 1)) + j % bm; // left wing
            int i2 = i1 ^ bm;                        // right wing
            std::complex<T> z1 = std::polar(T(1), ang * T(i1 ^ bw)); // left wing rotation
            std::complex<T> z2 = std::polar(T(1), ang * T(i2 ^ bw)); // right wing rotation
            std::complex<T> tmp = xo[i1];

            xo[i1]+= z1 * xo[i2];
            xo[i2] = tmp + z2 * xo[i2];
        }
    }

    return xo;
}


/*
 * Computes a 2D Fourier transform
 * with O(N^2 log N) complexity using the butterfly technique
 *
 * NOTE: the input must be a square matrix whose size is a power-of-two
 */
template <typename T> arg<T> eval_2d(const arg<T> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt2 = (int)xi.size();   // NxN
    int msb = findMSB(cnt2) / 2; // lg2(N) = lg2(sqrt(NxN))
    int cnt = 1 << msb;          // N = 2^lg2(N)
    T nrm = T(1) / T(cnt);
    arg<T> xo(cnt2);

    // pre-process the input data
    for (int j2 = 0; j2 < cnt; ++j2)
    for (int j1 = 0; j1 < cnt; ++j1) {
        int k2 = bitr(j2, msb);
        int k1 = bitr(j1, msb);

        xo[j1 + cnt * j2] = nrm * xi[k1 + cnt * k2];
    }

    // fft passes
    for (int i = 0; i < msb; ++i) {
        int bm = 1 << i; // butterfly mask
        int bw = 2 << i; // butterfly width
        float ang = T(dir) * Pi / T(bm); // precomputation

        // fft butterflies
        for (int j2 = 0; j2 < (cnt/2); ++j2)
        for (int j1 = 0; j1 < (cnt/2); ++j1) {
            int i11 = ((j1 >> i) << (i + 1)) + j1 % bm; // xmin wing
            int i21 = ((j2 >> i) << (i + 1)) + j2 % bm; // ymin wing
            int i12 = i11 ^ bm;                         // xmax wing
            int i22 = i21 ^ bm;                         // ymax wing
            int k11 = i11 + cnt * i21; // array offset
            int k12 = i12 + cnt * i21; // array offset
            int k21 = i11 + cnt * i22; // array offset
            int k22 = i12 + cnt * i22; // array offset

            // FFT-X
            std::complex<T> z11 = std::polar(T(1), ang * T(i11 ^ bw)); // left rotation
            std::complex<T> z12 = std::polar(T(1), ang * T(i12 ^ bw)); // right rotation
            std::complex<T> tmp1 = xo[k11];
            std::complex<T> tmp2 = xo[k21];

            xo[k11]+= z11 * xo[k12];
            xo[k12] = tmp1 + z12 * xo[k12];
            xo[k21]+= z11 * xo[k22];
            xo[k22] = tmp2 + z12 * xo[k22];

            // FFT-Y
            std::complex<T> z21 = std::polar(T(1), ang * T(i21 ^ bw)); // top rotation
            std::complex<T> z22 = std::polar(T(1), ang * T(i22 ^ bw)); // bottom rotation
            std::complex<T> tmp3 = xo[k11];
            std::complex<T> tmp4 = xo[k12];

            xo[k11]+= z21 * xo[k21];
            xo[k21] = tmp3 + z22 * xo[k21];
            xo[k12]+= z21 * xo[k22];
            xo[k22] = tmp4 + z22 * xo[k22];
        }
    }

    return xo;
}

/*
 * Computes a 3D Fourier transform
 * with O(N^3 log N) complexity using the butterfly technique
 *
 * NOTE: the input must be a square matrix whose size is a power-of-two
 */
template <typename T> arg<T> eval_3d(const arg<T> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt3 = (int)xi.size();   // NxNxN
    int msb = findMSB(cnt3) / 3; // lg2(N) = lg2(cbrt(NxNxN))
    int cnt = 1 << msb;          // N = 2^lg2(N)
    T nrm = T(1) / (T(cnt) * std::sqrt(T(cnt)));
    arg<T> xo(cnt3);

    // pre-process the input data
    for (int j3 = 0; j3 < cnt; ++j3)
    for (int j2 = 0; j2 < cnt; ++j2)
    for (int j1 = 0; j1 < cnt; ++j1) {
        int k3 = bitr(j3, msb);
        int k2 = bitr(j2, msb);
        int k1 = bitr(j1, msb);

        xo[j1 + cnt * (j2 + cnt * j3)] = nrm * xi[k1 + cnt * (k2 + cnt * k3)];
    }

    // fft passes
    for (int i = 0; i < msb; ++i) {
        int bm = 1 << i; // butterfly mask
        int bw = 2 << i; // butterfly width
        float ang = T(dir) * Pi / T(bm); // precomputation

        // fft butterflies
        for (int j3 = 0; j3 < (cnt/2); ++j3)
        for (int j2 = 0; j2 < (cnt/2); ++j2)
        for (int j1 = 0; j1 < (cnt/2); ++j1) {
            int i11 = ((j1 >> i) << (i + 1)) + j1 % bm; // xmin wing
            int i21 = ((j2 >> i) << (i + 1)) + j2 % bm; // ymin wing
            int i31 = ((j3 >> i) << (i + 1)) + j3 % bm; // zmin wing
            int i12 = i11 ^ bm;                         // xmax wing
            int i22 = i21 ^ bm;                         // ymax wing
            int i32 = i31 ^ bm;                         // zmax wing
            int k111 = i11 + cnt * (i21 + cnt * i31); // array offset
            int k121 = i12 + cnt * (i21 + cnt * i31); // array offset
            int k211 = i11 + cnt * (i22 + cnt * i31); // array offset
            int k221 = i12 + cnt * (i22 + cnt * i31); // array offset
            int k112 = i11 + cnt * (i21 + cnt * i32); // array offset
            int k122 = i12 + cnt * (i21 + cnt * i32); // array offset
            int k212 = i11 + cnt * (i22 + cnt * i32); // array offset
            int k222 = i12 + cnt * (i22 + cnt * i32); // array offset

            // FFT-X
            std::complex<T> z11 = std::polar(T(1), ang * T(i11 ^ bw)); // left rotation
            std::complex<T> z12 = std::polar(T(1), ang * T(i12 ^ bw)); // right rotation
            std::complex<T> tmp01 = xo[k111];
            std::complex<T> tmp02 = xo[k211];
            std::complex<T> tmp03 = xo[k112];
            std::complex<T> tmp04 = xo[k212];

            xo[k111]+= z11 * xo[k121];
            xo[k121] = tmp01 + z12 * xo[k121];
            xo[k211]+= z11 * xo[k221];
            xo[k221] = tmp02 + z12 * xo[k221];
            xo[k112]+= z11 * xo[k122];
            xo[k122] = tmp03 + z12 * xo[k122];
            xo[k212]+= z11 * xo[k222];
            xo[k222] = tmp04 + z12 * xo[k222];

            // FFT-Y
            std::complex<T> z21 = std::polar(T(1), ang * T(i21 ^ bw)); // top rotation
            std::complex<T> z22 = std::polar(T(1), ang * T(i22 ^ bw)); // bottom rotation
            std::complex<T> tmp05 = xo[k111];
            std::complex<T> tmp06 = xo[k121];
            std::complex<T> tmp07 = xo[k112];
            std::complex<T> tmp08 = xo[k122];

            xo[k111]+= z21 * xo[k211];
            xo[k211] = tmp05 + z22 * xo[k211];
            xo[k121]+= z21 * xo[k221];
            xo[k221] = tmp06 + z22 * xo[k221];
            xo[k112]+= z21 * xo[k212];
            xo[k212] = tmp07 + z22 * xo[k212];
            xo[k122]+= z21 * xo[k222];
            xo[k222] = tmp08 + z22 * xo[k222];

            // FFT-Z
            std::complex<T> z31 = std::polar(T(1), ang * T(i31 ^ bw)); // front rotation
            std::complex<T> z32 = std::polar(T(1), ang * T(i32 ^ bw)); // back rotation
            std::complex<T> tmp09 = xo[k111];
            std::complex<T> tmp10 = xo[k121];
            std::complex<T> tmp11 = xo[k211];
            std::complex<T> tmp12 = xo[k221];

            xo[k111]+= z31 * xo[k112];
            xo[k112] = tmp09 + z32 * xo[k112];
            xo[k121]+= z31 * xo[k122];
            xo[k122] = tmp10 + z32 * xo[k122];
            xo[k211]+= z31 * xo[k212];
            xo[k212] = tmp11 + z32 * xo[k212];
            xo[k221]+= z31 * xo[k222];
            xo[k222] = tmp12 + z32 * xo[k222];
        }
    }

    return xo;
}

#ifdef DJ_FFT_IMPLEMENTATION
#ifdef DJ_FFT_ENABLE_GPU
static const char *s_ComputeShaderSrc = {
    "uniform float u_Dir;   // FFT direction\n"
    "uniform int u_ArgSize; // N\n"
    "uniform int u_PassID;  // pass number in [0, lg2(N))\n\n"

    "layout (local_size_x = 1,  local_size_y = 1, local_size_z = 1) in;\n\n"

    "vec2 expi(float ang) { return vec2(cos(ang), sin(ang)); }\n"
    "vec2 zmul(vec2 z1, vec2 z2) {\n"
    "    return vec2(z1.x * z2.x - z1.y * z2.y,\n"
    "                z1.x * z2.y + z1.y * z2.x);\n"
    "}\n\n"

    "#ifdef FFT_1D\n"
    "layout (rg32f) uniform coherent image1D u_Arg; // FFT arg\n"
    "void main()\n"
    "{\n"
    "    int j = int(gl_GlobalInvocationID.x);\n\n"

    "    if (j >= u_ArgSize/2)\n"
    "        return;\n\n"

    "   const float pi = 3.141592653589793238462643383279502884f;\n"
    "    int i = u_PassID;\n"
    "    int bm = 1 << i;\n"
    "    int bw = 2 << i;\n"
    "    float ang = u_Dir * pi / float(bm);\n"
    "    int i1 = ((j >> i) << (i + 1)) + j % bm; // xmin wing\n"
    "    int i2 = i1 ^ bm;                        // xmax wing\n"
    "    vec2 z1 = expi(ang * float(i1 ^ bw));\n"
    "    vec2 z2 = expi(ang * float(i2 ^ bw));\n"
    "    vec2 b1 = imageLoad(u_Arg, i1).xy;\n"
    "    vec2 b2 = imageLoad(u_Arg, i2).xy;\n\n"

    "    imageStore(u_Arg, i1, vec4(b1 + zmul(z1, b2), 0, 0));\n"
    "    imageStore(u_Arg, i2, vec4(b1 + zmul(z2, b2), 0, 0));\n"
    "}\n"
    "#endif\n\n"

    "#ifdef FFT_2D\n"
    "layout (rg32f) uniform coherent image2D u_Arg; // FFT arg\n"
    "void main()\n"
    "{\n"
    "    int j1 = int(gl_GlobalInvocationID.x);\n"
    "    int j2 = int(gl_GlobalInvocationID.y);\n\n"
    ""
    "    if (j1 >= u_ArgSize/2 || j2 >= u_ArgSize/2)\n"
    "        return;\n\n"
    ""
    "    const float pi = 3.141592653589793238462643383279502884f;\n"
    "    int i = u_PassID;\n"
    "    int bm = 1 << i;\n"
    "    int bw = 2 << i;\n"
    "    float ang = u_Dir * pi / float(bm);\n"
    "    int i11 = ((j1 >> i) << (i + 1)) + j1 % bm; // xmin wing\n"
    "    int i21 = ((j2 >> i) << (i + 1)) + j2 % bm; // ymin wing\n"
    "    int i12 = i11 ^ bm;                         // xmax wing\n"
    "    int i22 = i21 ^ bm;                         // ymax wing\n"
    "    ivec2 k11 = ivec2(i11, i21);\n"
    "    ivec2 k12 = ivec2(i12, i21);\n"
    "    ivec2 k21 = ivec2(i11, i22);\n"
    "    ivec2 k22 = ivec2(i12, i22);\n\n"
    ""
    "    // FFT-X\n"
    "    {\n"
    "        vec2 b11 = imageLoad(u_Arg, k11).xy;\n"
    "        vec2 b12 = imageLoad(u_Arg, k12).xy;\n"
    "        vec2 b21 = imageLoad(u_Arg, k21).xy;\n"
    "        vec2 b22 = imageLoad(u_Arg, k22).xy;\n"
    "        vec2 z11 = expi(ang * float(i11 ^ bw));\n"
    "        vec2 z12 = expi(ang * float(i12 ^ bw));\n\n"
    ""
    "        imageStore(u_Arg, k11, vec4(b11 + zmul(z11, b12), 0, 0));\n"
    "        imageStore(u_Arg, k12, vec4(b11 + zmul(z12, b12), 0, 0));\n"
    "        imageStore(u_Arg, k21, vec4(b21 + zmul(z11, b22), 0, 0));\n"
    "        imageStore(u_Arg, k22, vec4(b21 + zmul(z12, b22), 0, 0));\n"
    "    }\n\n"
    ""
    "    // FFT-Y\n"
    "    {\n"
    "        vec2 b11 = imageLoad(u_Arg, k11).xy;\n"
    "        vec2 b12 = imageLoad(u_Arg, k12).xy;\n"
    "        vec2 b21 = imageLoad(u_Arg, k21).xy;\n"
    "        vec2 b22 = imageLoad(u_Arg, k22).xy;\n"
    "        vec2 z21 = expi(ang * float(i21 ^ bw));\n"
    "        vec2 z22 = expi(ang * float(i22 ^ bw));\n\n"
    ""
    "        imageStore(u_Arg, k11, vec4(b11 + zmul(z21, b21), 0, 0));\n"
    "        imageStore(u_Arg, k21, vec4(b11 + zmul(z22, b21), 0, 0));\n"
    "        imageStore(u_Arg, k12, vec4(b12 + zmul(z21, b22), 0, 0));\n"
    "        imageStore(u_Arg, k22, vec4(b12 + zmul(z22, b22), 0, 0));\n"
    "    }\n"
    "}\n"
    "#endif\n\n"
    ""
    "#ifdef FFT_3D\n"
    "layout (rg32f) uniform image3D u_Arg; // FFT arg\n"
    "void main()\n"
    "{\n"
    "    int j1 = int(gl_GlobalInvocationID.x);\n"
    "    int j2 = int(gl_GlobalInvocationID.y);\n"
    "    int j3 = int(gl_GlobalInvocationID.z);\n\n"
    ""
    "    if (j1 >= u_ArgSize/2 || j2 >= u_ArgSize/2 || j3 >= u_ArgSize/2)\n"
    "        return;\n\n"
    ""
    "    const float pi = 3.141592653589793238462643383279502884f;\n"
    "    int i = u_PassID;\n"
    "    int bm = 1 << i;\n"
    "    int bw = 2 << i;\n"
    "    float ang = u_Dir * pi / float(bm);\n"
    "    int i11 = ((j1 >> i) << (i + 1)) + j1 % bm; // xmin wing\n"
    "    int i21 = ((j2 >> i) << (i + 1)) + j2 % bm; // ymin wing\n"
    "    int i31 = ((j3 >> i) << (i + 1)) + j3 % bm; // zmin wing\n"
    "    int i12 = i11 ^ bm;                         // xmax wing\n"
    "    int i22 = i21 ^ bm;                         // ymax wing\n"
    "    int i32 = i31 ^ bm;                         // zmax wing\n"
    "    ivec3 k111 = ivec3(i11, i21, i31);\n"
    "    ivec3 k121 = ivec3(i12, i21, i31);\n"
    "    ivec3 k211 = ivec3(i11, i22, i31);\n"
    "    ivec3 k221 = ivec3(i12, i22, i31);\n"
    "    ivec3 k112 = ivec3(i11, i21, i32);\n"
    "    ivec3 k122 = ivec3(i12, i21, i32);\n"
    "    ivec3 k212 = ivec3(i11, i22, i32);\n"
    "    ivec3 k222 = ivec3(i12, i22, i32);\n\n"
    ""
    "    // FFT-X\n"
    "    {\n"
    "        vec2 b111 = imageLoad(u_Arg, k111).xy;\n"
    "        vec2 b121 = imageLoad(u_Arg, k121).xy;\n"
    "        vec2 b211 = imageLoad(u_Arg, k211).xy;\n"
    "        vec2 b221 = imageLoad(u_Arg, k221).xy;\n"
    "        vec2 b112 = imageLoad(u_Arg, k112).xy;\n"
    "        vec2 b122 = imageLoad(u_Arg, k122).xy;\n"
    "        vec2 b212 = imageLoad(u_Arg, k212).xy;\n"
    "        vec2 b222 = imageLoad(u_Arg, k222).xy;\n"
    "        vec2 z11 = expi(ang * float(i11 ^ bw));\n"
    "        vec2 z12 = expi(ang * float(i12 ^ bw));\n\n"
    ""
    "        imageStore(u_Arg, k111, vec4(b111 + zmul(z11, b121), 0, 0));\n"
    "        imageStore(u_Arg, k121, vec4(b111 + zmul(z12, b121), 0, 0));\n"
    "        imageStore(u_Arg, k211, vec4(b211 + zmul(z11, b221), 0, 0));\n"
    "        imageStore(u_Arg, k221, vec4(b211 + zmul(z12, b221), 0, 0));\n"
    "        imageStore(u_Arg, k112, vec4(b112 + zmul(z11, b122), 0, 0));\n"
    "        imageStore(u_Arg, k122, vec4(b112 + zmul(z12, b122), 0, 0));\n"
    "        imageStore(u_Arg, k212, vec4(b212 + zmul(z11, b222), 0, 0));\n"
    "        imageStore(u_Arg, k222, vec4(b212 + zmul(z12, b222), 0, 0));\n"
    "    }\n\n"
    ""
    "    // FFT-Y\n"
    "    {\n"
    "        vec2 b111 = imageLoad(u_Arg, k111).xy;\n"
    "        vec2 b121 = imageLoad(u_Arg, k121).xy;\n"
    "        vec2 b211 = imageLoad(u_Arg, k211).xy;\n"
    "        vec2 b221 = imageLoad(u_Arg, k221).xy;\n"
    "        vec2 b112 = imageLoad(u_Arg, k112).xy;\n"
    "        vec2 b122 = imageLoad(u_Arg, k122).xy;\n"
    "        vec2 b212 = imageLoad(u_Arg, k212).xy;\n"
    "        vec2 b222 = imageLoad(u_Arg, k222).xy;\n"
    "        vec2 z21 = expi(ang * float(i21 ^ bw));\n"
    "        vec2 z22 = expi(ang * float(i22 ^ bw));\n\n"

    "        imageStore(u_Arg, k111, vec4(b111 + zmul(z21, b211), 0, 0));\n"
    "        imageStore(u_Arg, k211, vec4(b111 + zmul(z22, b211), 0, 0));\n"
    "        imageStore(u_Arg, k121, vec4(b121 + zmul(z21, b221), 0, 0));\n"
    "        imageStore(u_Arg, k221, vec4(b121 + zmul(z22, b221), 0, 0));\n"
    "        imageStore(u_Arg, k112, vec4(b112 + zmul(z21, b212), 0, 0));\n"
    "        imageStore(u_Arg, k212, vec4(b112 + zmul(z22, b212), 0, 0));\n"
    "        imageStore(u_Arg, k122, vec4(b122 + zmul(z21, b222), 0, 0));\n"
    "        imageStore(u_Arg, k222, vec4(b122 + zmul(z22, b222), 0, 0));\n"
    "    }\n\n"
    ""
    "    // FFT-Z\n"
    "    {\n"
    "        vec2 b111 = imageLoad(u_Arg, k111).xy;\n"
    "        vec2 b121 = imageLoad(u_Arg, k121).xy;\n"
    "        vec2 b211 = imageLoad(u_Arg, k211).xy;\n"
    "        vec2 b221 = imageLoad(u_Arg, k221).xy;\n"
    "        vec2 b112 = imageLoad(u_Arg, k112).xy;\n"
    "        vec2 b122 = imageLoad(u_Arg, k122).xy;\n"
    "        vec2 b212 = imageLoad(u_Arg, k212).xy;\n"
    "        vec2 b222 = imageLoad(u_Arg, k222).xy;\n"
    "        vec2 z31 = expi(ang * float(i31 ^ bw));\n"
    "        vec2 z32 = expi(ang * float(i32 ^ bw));\n\n"
    ""
    "        imageStore(u_Arg, k111, vec4(b111 + zmul(z31, b112), 0, 0));\n"
    "        imageStore(u_Arg, k112, vec4(b111 + zmul(z32, b112), 0, 0));\n"
    "        imageStore(u_Arg, k121, vec4(b121 + zmul(z31, b122), 0, 0));\n"
    "        imageStore(u_Arg, k122, vec4(b121 + zmul(z32, b122), 0, 0));\n"
    "        imageStore(u_Arg, k211, vec4(b211 + zmul(z31, b212), 0, 0));\n"
    "        imageStore(u_Arg, k212, vec4(b211 + zmul(z32, b212), 0, 0));\n"
    "        imageStore(u_Arg, k221, vec4(b221 + zmul(z31, b222), 0, 0));\n"
    "        imageStore(u_Arg, k222, vec4(b221 + zmul(z32, b222), 0, 0));\n"
    "    }\n"
    "}\n"
    "#endif\n"
};

/*
 * Compute a 1D FFT on the GPU
 */
arg<float> eval_1d_gpu(const arg<float> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt = (int)xi.size();
    int msb = findMSB(cnt);
    float nrm = float(1) / std::sqrt(cnt);
    arg<float> xo(cnt);
    struct {
        GLuint texture, program;
        struct {GLint passID;} uniformLocations;
    } gl;

    // pre-process the input data
    for (int j = 0; j < cnt; ++j)
        xo[j] = nrm * xi[bitr(j, msb)];

    // upload data to GPU
    glGenTextures(1, &gl.texture);
    glBindTexture(GL_TEXTURE_1D, gl.texture);
    glTexStorage1D(GL_TEXTURE_1D, 1, GL_RG32F, cnt);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, cnt, GL_RG, GL_FLOAT, &xo[0]);
    glBindImageTexture(0, gl.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG32F);

    // setup GPU Kernel
    {
        GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
        const GLchar *shaderSrc[] = {
            "#version 450 core\n",
            "#define FFT_1D\n",
            s_ComputeShaderSrc
        };

        // create program
        gl.program = glCreateProgram();

        // compile and attach shader
        glShaderSource(shader,
                       sizeof(shaderSrc) / sizeof(shaderSrc[0]),
                       shaderSrc,
                       NULL);
        glCompileShader(shader);
        glAttachShader(gl.program, shader);
        glDeleteShader(shader);

        // link program and setup uniforms
        glLinkProgram(gl.program);
        glUseProgram(gl.program);
        gl.uniformLocations.passID =
                    glGetUniformLocation(gl.program, "u_PassID");
        glUniform1f(glGetUniformLocation(gl.program, "u_Dir"), float(dir));
        glUniform1i(glGetUniformLocation(gl.program, "u_ArgSize"), cnt);
        glUniform1i(glGetUniformLocation(gl.program, "u_Arg"), 0);
    }

    // run
    for (int i = 0; i < msb; ++i) {
        glUniform1i(gl.uniformLocations.passID, i);
        glDispatchCompute(cnt, 1, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    // retrieve data
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RG, GL_FLOAT, &xo[0]);

    // cleanup GL state
    glDeleteProgram(gl.program);
    glDeleteTextures(1, &gl.texture);

    return xo;
}

/*
 * Compute a 2D FFT on the GPU
 */
arg<float> eval_2d_gpu(const arg<float> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt2 = (int)xi.size();   // NxN
    int msb = findMSB(cnt2) / 2; // lg2(N) = lg2(sqrt(NxN))
    int cnt = 1 << msb;          // N = 2^lg2(N)
    float nrm = float(1) / float(cnt);
    arg<float> xo(cnt2);
    struct {
        GLuint texture, program;
        struct {GLint passID;} uniformLocations;
    } gl;

    // pre-process the input data
    for (int j2 = 0; j2 < cnt; ++j2)
    for (int j1 = 0; j1 < cnt; ++j1) {
        int k2 = bitr(j2, msb);
        int k1 = bitr(j1, msb);

        xo[j1 + cnt * j2] = nrm * xi[k1 + cnt * k2];
    }

    // upload data to GPU
    glGenTextures(1, &gl.texture);
    glBindTexture(GL_TEXTURE_2D, gl.texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG32F, cnt, cnt);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cnt, cnt, GL_RG, GL_FLOAT, &xo[0]);
    glBindImageTexture(0, gl.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG32F);

    // setup GPU Kernel
    {
        GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
        const GLchar *shaderSrc[] = {
            "#version 450 core\n",
            "#define FFT_2D\n",
            s_ComputeShaderSrc
        };

        // create program
        gl.program = glCreateProgram();

        // compile and attach shader
        glShaderSource(shader,
                       sizeof(shaderSrc) / sizeof(shaderSrc[0]),
                       shaderSrc,
                       NULL);
        glCompileShader(shader);
        glAttachShader(gl.program, shader);
        glDeleteShader(shader);

        // link program and setup uniforms
        glLinkProgram(gl.program);
        glUseProgram(gl.program);
        gl.uniformLocations.passID =
                    glGetUniformLocation(gl.program, "u_PassID");
        glUniform1f(glGetUniformLocation(gl.program, "u_Dir"), float(dir));
        glUniform1i(glGetUniformLocation(gl.program, "u_ArgSize"), cnt);
        glUniform1i(glGetUniformLocation(gl.program, "u_Arg"), 0);
    }

    // run
    for (int i = 0; i < msb; ++i) {
        glUniform1i(gl.uniformLocations.passID, i);
        glDispatchCompute(cnt, cnt, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    // retrieve data
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_FLOAT, &xo[0]);

    // cleanup GL state
    glDeleteProgram(gl.program);
    glDeleteTextures(1, &gl.texture);

    return xo;
}

/*
 * Compute a 3D FFT on the GPU
 */
arg<float> eval_3d_gpu(const arg<float> &xi, const e_dir &dir)
{
    DJ_ASSERT((xi.size() & (xi.size() - 1)) == 0 && "invalid input size");
    int cnt3 = (int)xi.size();   // NxNxN
    int msb = findMSB(cnt3) / 3; // lg2(N) = lg2(cbrt(NxNxN))
    int cnt = 1 << msb;          // N = 2^lg2(N)
    float nrm = float(1) / (float(cnt) * std::sqrt(float(cnt)));
    arg<float> xo(cnt3);
    struct {
        GLuint texture, program;
        struct {GLint passID;} uniformLocations;
    } gl;

    // pre-process the input data
    for (int j3 = 0; j3 < cnt; ++j3)
    for (int j2 = 0; j2 < cnt; ++j2)
    for (int j1 = 0; j1 < cnt; ++j1) {
        int k3 = bitr(j3, msb);
        int k2 = bitr(j2, msb);
        int k1 = bitr(j1, msb);

        xo[j1 + cnt * (j2 + cnt * j3)] = nrm * xi[k1 + cnt * (k2 + cnt * k3)];
    }

    // upload data to GPU
    glGenTextures(1, &gl.texture);
    glBindTexture(GL_TEXTURE_3D, gl.texture);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RG32F, cnt, cnt, cnt);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, cnt, cnt, cnt,
                    GL_RG, GL_FLOAT, &xo[0]);
    glBindImageTexture(0, gl.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG32F);

    // setup GPU Kernel
    {
        GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
        const GLchar *shaderSrc[] = {
            "#version 450 core\n",
            "#define FFT_3D\n",
            s_ComputeShaderSrc
        };

        // create program
        gl.program = glCreateProgram();

        // compile and attach shader
        glShaderSource(shader,
                       sizeof(shaderSrc) / sizeof(shaderSrc[0]),
                       shaderSrc,
                       NULL);
        glCompileShader(shader);
        glAttachShader(gl.program, shader);
        glDeleteShader(shader);

        // link program and setup uniforms
        glLinkProgram(gl.program);
        glUseProgram(gl.program);
        gl.uniformLocations.passID =
                    glGetUniformLocation(gl.program, "u_PassID");
        glUniform1f(glGetUniformLocation(gl.program, "u_Dir"), float(dir));
        glUniform1i(glGetUniformLocation(gl.program, "u_ArgSize"), cnt);
        glUniform1i(glGetUniformLocation(gl.program, "u_Arg"), 0);
    }

    // run
    for (int i = 0; i < msb; ++i) {
        glUniform1i(gl.uniformLocations.passID, i);
        glDispatchCompute(cnt, cnt, cnt);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    // retrieve data
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RG, GL_FLOAT, &xo[0]);

    // cleanup GL state
    glDeleteProgram(gl.program);
    glDeleteTextures(1, &gl.texture);

    return xo;
}
#endif // DJ_FFT_ENABLE_GPU
#endif // DJ_FFT_IMPLEMENTATION

} // namespace fft
} // namespace dj

#endif // DJ_INCLUDE_FFT_H
