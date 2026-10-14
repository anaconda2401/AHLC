#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <cmath>
#include <iomanip>
#include <algorithm>

#include "analyzer/analyzer.hpp"
#include "bitio/bit_writer.hpp"
#include "bitio/bit_reader.hpp"
#include "rle/rle_encoder.hpp"
#include "rle/rle_decoder.hpp"
#include "image/image.hpp"
#include "image/image_predictor.hpp"
#include "image/eabps.hpp"
#include "audio/audio.hpp"
#include "audio/audio_predictor.hpp"
#include "entropy/huffman.hpp"
#include "media/format_detector.hpp"

// FFmpeg (linked via vcpkg — avutil, avcodec, avformat, swscale, swresample)
extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

static constexpr double PI = 3.14159265358979323846;

// ================================================================
// CLI helpers
// ================================================================
static bool has_flag(int argc, char* argv[], const std::string& flag)
{
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == flag) return true;
    return false;
}
static std::string get_option_val(int argc, char* argv[], const std::string& opt, const std::string& def = "")
{
    for (int i = 2; i < argc - 1; ++i) {
        if (std::string(argv[i]) == opt) return argv[i + 1];
    }
    return def;
}
static bool write_bytes(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()),
                                static_cast<std::streamsize>(data.size()));
    return true;
}
static bool read_bytes(const std::string& path, std::vector<uint8_t>& data)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto sz = f.tellg(); f.seekg(0);
    data.resize(static_cast<size_t>(sz));
    if (sz > 0) f.read(reinterpret_cast<char*>(data.data()), sz);
    return true;
}
// Simple JSON string escape (backslash and double-quote only)
static std::string jstr(const std::string& s)
{
    std::string r = "\"";
    for (char c : s) {
        if (c == '\\') r += "\\\\";
        else if (c == '"') r += "\\\"";
        else r += c;
    }
    r += '"';
    return r;
}

// ================================================================
// bitio-test
// ================================================================
static bool run_bitio_test()
{
    std::cout << "AHLC Bit I/O Test\n-----------------\n";
    const std::string tf = "bitio_test_tmp.bin";
    const std::vector<uint8_t> sb = {1,0,1,1,0};
    struct BV { uint64_t v; int b; };
    const std::vector<BV> iv = {{5,3},{17,5},{255,8}};
    { ahlc::BitWriter w; if(!w.open(tf)){std::cout<<"BitWriter: FAIL\n";return false;}
      for(auto b:sb)w.write_bit(b); for(auto& v:iv)w.write_bits(v.v,v.b); w.close(); }
    std::cout<<"BitWriter: PASS\n";
    bool ok=true;
    { ahlc::BitReader r; if(!r.open(tf)){std::cout<<"BitReader: FAIL\n";return false;}
      for(auto e:sb){uint8_t g=0;if(!r.read_bit(g)||g!=e){ok=false;break;}}
      if(ok) for(auto& v:iv){uint64_t g=0;if(!r.read_bits(g,v.b)||g!=v.v){ok=false;break;}}
      if(ok&&!r.eof())ok=false; r.close(); }
    std::cout<<"BitReader: "<<(ok?"PASS":"FAIL")<<"\n";
    std::remove(tf.c_str());
    std::cout<<"Round-trip: "<<(ok?"PASS":"FAIL")<<"\n";
    return ok;
}

// ================================================================
// rle-test
// ================================================================
static bool rle_rt(const std::string& name, const std::vector<uint8_t>& orig)
{
    const std::string fo="__rl_o.tmp",fc="__rl_c.tmp",fd="__rl_d.tmp";
    if(!write_bytes(fo,orig)){std::cout<<"  [FAIL] "<<name<<"\n";return false;}
    ahlc::RleStats s;
    if(!ahlc::rle_compress(fo,fc,s)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(fo.c_str());return false;}
    if(!ahlc::rle_decompress(fc,fd)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(fo.c_str());std::remove(fc.c_str());return false;}
    bool ok=ahlc::files_identical(fo,fd);
    char buf[32];std::snprintf(buf,sizeof(buf),"%.2fx",s.original_size>0?(double)s.original_size/(s.compressed_size?s.compressed_size:1):0.0);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<" | orig="<<s.original_size<<"B comp="<<s.compressed_size<<"B"<<(s.original_size>0?std::string(" (")+buf+")":"")<<"\n";
    std::remove(fo.c_str());std::remove(fc.c_str());std::remove(fd.c_str());
    return ok;
}
static bool run_rle_test()
{
    std::cout<<"AHLC RLE Round-Trip Tests\n-------------------------\n";
    bool a=true;
    {std::vector<uint8_t> d(200,'A');a&=rle_rt("Highly repetitive (200x 'A')",d);}
    {std::vector<uint8_t> d(256);for(int i=0;i<256;++i)d[static_cast<size_t>(i)]=static_cast<uint8_t>(i);a&=rle_rt("No repetition (bytes 0-255)",d);}
    {std::vector<uint8_t> d(500,0xFF);a&=rle_rt("Long run (500x 0xFF)",d);}
    {std::vector<uint8_t> d(100);for(int i=0;i<100;++i)d[i]=(i%2)?0xFF:0x00;a&=rle_rt("Alternating 0x00/0xFF",d);}
    {a&=rle_rt("Empty (0 bytes)",{});}
    {a&=rle_rt("Single byte (0xAB)",{0xAB});}
    {std::vector<uint8_t> d;for(char c:"Hello")d.push_back(c);for(int i=0;i<50;++i)d.push_back('Z');for(char c:"World!")d.push_back(c);a&=rle_rt("Mixed",d);}
    std::cout<<"-------------------------\nOverall: "<<(a?"ALL PASS":"SOME FAILED")<<"\n";
    return a;
}

// ================================================================
// huffman-test
// ================================================================
static bool huff_rt(const std::string& name, const std::vector<uint8_t>& orig)
{
    std::vector<uint8_t> compressed,recovered;
    if(!ahlc::huffman_compress_mem(orig,compressed)){std::cout<<"  [FAIL] "<<name<<" — compress\n";return false;}
    if(!ahlc::huffman_decompress_mem(compressed,recovered)){std::cout<<"  [FAIL] "<<name<<" — decompress\n";return false;}
    bool ok=(orig==recovered);
    double ratio=(compressed.size()>0)?(double)orig.size()/compressed.size():0.0;
    double saved=(orig.size()>0)?(1.0-(double)compressed.size()/orig.size())*100.0:0.0;
    bool seen[256]={};for(uint8_t b:orig)seen[b]=true; int uniq=0;for(bool v:seen)if(v)++uniq;
    double freq[256]={};for(uint8_t b:orig)freq[b]++;
    double ent=0.0,n=(double)orig.size();if(n>0)for(double f:freq)if(f>0){double p=f/n;ent-=p*std::log2(p);}
    char rbuf[16],sbuf[16],ebuf[16];
    std::snprintf(rbuf,sizeof(rbuf),"%.3fx",ratio);
    std::snprintf(sbuf,sizeof(sbuf),"%.1f%%",saved);
    std::snprintf(ebuf,sizeof(ebuf),"%.4f",ent);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<" | orig="<<orig.size()<<"B comp="<<compressed.size()<<"B ratio="<<rbuf<<" saved="<<sbuf<<" sym="<<uniq<<" H="<<ebuf<<"\n";
    return ok;
}
static ahlc::BMPImage make_image(uint32_t w,uint32_t h,
    std::function<void(uint32_t,uint32_t,uint8_t&,uint8_t&,uint8_t&)> fill)
{
    ahlc::BMPImage img;img.width=w;img.height=h;img.bpp=24;img.pixels.resize(w*h*3);
    for(uint32_t r=0;r<h;++r) for(uint32_t c=0;c<w;++c){
        uint8_t B=0,G=0,R=0;fill(r,c,B,G,R);
        uint32_t b=(r*w+c)*3;img.pixels[b]=B;img.pixels[b+1]=G;img.pixels[b+2]=R;
    }
    return img;
}
static bool run_huffman_test()
{
    std::cout<<"AHLC Huffman Round-Trip Tests\n-----------------------------\n";
    bool all=true;
    all&=huff_rt("Empty input (0 bytes)",{});
    {std::vector<uint8_t> d(1000,0xAA);all&=huff_rt("Single symbol (1000x 0xAA)",d);}
    {std::vector<uint8_t> d(500,0x00);for(int i=0;i<250;++i)d[static_cast<size_t>(i*2+1)]=0xFF;all&=huff_rt("Two symbols (0x00/0xFF)",d);}
    {std::vector<uint8_t> d(1000,'A');for(int i=0;i<100;++i)d[static_cast<size_t>(i*9)]=static_cast<uint8_t>(i+1);all&=huff_rt("Skewed (90% 'A')",d);}
    {std::vector<uint8_t> d(256);for(int i=0;i<256;++i)d[static_cast<size_t>(i)]=static_cast<uint8_t>(i);all&=huff_rt("All 256 symbols (1 each)",d);}
    {std::vector<uint8_t> d(2048);for(int i=0;i<2048;++i)d[static_cast<size_t>(i)]=static_cast<uint8_t>(i&0xFF);all&=huff_rt("Sequential bytes (uniform)",d);}
    {std::vector<uint8_t> d(4000);uint32_t s=0xDEADBEEFu;for(auto& b:d){s=s*1664525u+1013904223u;b=static_cast<uint8_t>(s>>24);}all&=huff_rt("Pseudo-random (4000 bytes)",d);}
    {std::vector<uint8_t> bin;if(read_bytes("build/Release/ahmc.exe",bin)&&!bin.empty())all&=huff_rt("Binary file (ahmc.exe)",bin);else std::cout<<"  [SKIP] Binary file\n";}
    {auto img=make_image(64,64,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){B=static_cast<uint8_t>((r+c)&0xFF);G=static_cast<uint8_t>((r*2+c)&0xFF);R=static_cast<uint8_t>((r+c*2)&0xFF);});
     auto res=ahlc::encode_spatial_predictor(img);all&=huff_rt("Image residual data (64x64)",res);}
    {const int N=4000;ahlc::AudioBuffer audio;audio.format=1;audio.channels=1;audio.sample_rate=44100;audio.bits_per_sample=16;audio.total_samples=N;audio.pcm.resize(N*2);
     for(int i=0;i<N;++i){int16_t s=static_cast<int16_t>(20000.0*std::sin(2.0*PI*440.0*i/44100.0));audio.pcm[static_cast<size_t>(i)*2]=static_cast<uint8_t>(s&0xFF);audio.pcm[static_cast<size_t>(i)*2+1]=static_cast<uint8_t>((s>>8)&0xFF);}
     auto res=ahlc::encode_audio_predictor(audio);all&=huff_rt("Audio residual data (4000 sine samps)",res);}
    std::cout<<"-----------------------------\nOverall: "<<(all?"ALL PASS":"SOME FAILED")<<"\n";
    return all;
}

// ================================================================
// image-test
// ================================================================
static bool image_rt(const std::string& name, const ahlc::BMPImage& orig)
{
    const std::string bi="__ig_i.bmp",ah="__ig.ahmc",bo="__ig_o.bmp";
    if(!ahlc::write_bmp(bi,orig)){std::cout<<"  [FAIL] "<<name<<"\n";return false;}
    ahlc::ImageCompressStats s;
    if(!ahlc::compress_image(bi,ah,s)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());return false;}
    if(!ahlc::decompress_image(ah,bo)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());std::remove(ah.c_str());return false;}
    ahlc::BMPImage rec;if(!ahlc::read_bmp(bo,rec)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());std::remove(ah.c_str());std::remove(bo.c_str());return false;}
    bool ok=ahlc::images_equal(orig,rec);
    const char* cn=(s.codec_used==ahlc::CODEC_HUFFMAN)?"HUFFMAN":"RLE";
    char buf[32];std::snprintf(buf,sizeof(buf),"%.2fx",s.compressed_file_size>0?(double)s.original_file_size/s.compressed_file_size:0.0);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<"  ("<<orig.width<<"x"<<orig.height<<" codec="<<cn<<" ratio="<<buf<<")\n";
    std::remove(bi.c_str());std::remove(ah.c_str());std::remove(bo.c_str());
    return ok;
}
static bool run_image_test()
{
    std::cout<<"AHLC Image Round-Trip Tests\n---------------------------\n";
    bool a=true;
    a&=image_rt("Uniform (all green)",make_image(8,8,[](uint32_t,uint32_t,uint8_t&B,uint8_t&G,uint8_t&R){B=0;G=200;R=0;}));
    a&=image_rt("Horizontal gradient",make_image(16,8,[](uint32_t,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){uint8_t v=static_cast<uint8_t>(c*16);B=v;G=v;R=v;}));
    a&=image_rt("Vertical gradient",make_image(8,16,[](uint32_t r,uint32_t,uint8_t&B,uint8_t&G,uint8_t&R){uint8_t v=static_cast<uint8_t>(r*16);B=v;G=v;R=v;}));
    a&=image_rt("Checkerboard",make_image(8,8,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){bool w=(r+c)%2==0;B=w?255:0;G=w?255:0;R=w?255:0;}));
    a&=image_rt("Sequential bytes",make_image(16,16,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){uint8_t v=static_cast<uint8_t>((r*16+c)&0xFF);B=v;G=static_cast<uint8_t>(v+85);R=static_cast<uint8_t>(v+170);}));
    a&=image_rt("Color blocks",make_image(8,8,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){bool t=r<4,l=c<4;if(t&&l){B=0;G=0;R=255;}if(t&&!l){B=0;G=255;R=0;}if(!t&&l){B=255;G=0;R=0;}if(!t&&!l){B=255;G=255;R=255;}}));
    a&=image_rt("Single pixel",make_image(1,1,[](uint32_t,uint32_t,uint8_t&B,uint8_t&G,uint8_t&R){B=0xAB;G=0xCD;R=0xEF;}));
    std::cout<<"---------------------------\nOverall: "<<(a?"ALL PASS":"SOME FAILED")<<"\n";
    return a;
}

// ================================================================
// image-adaptive-test
// ================================================================
static bool image_adaptive_rt(const std::string& name, const ahlc::BMPImage& orig)
{
    const std::string bi="__iga_i.bmp",ah="__iga.ahmc",bo="__iga_o.bmp";
    if(!ahlc::write_bmp(bi,orig)){std::cout<<"  [FAIL] "<<name<<"\n";return false;}
    ahlc::ImageCompressStats s;
    if(!ahlc::compress_image(bi,ah,s)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());return false;}
    if(!ahlc::decompress_image(ah,bo)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());std::remove(ah.c_str());return false;}
    ahlc::BMPImage rec;
    if(!ahlc::read_bmp(bo,rec)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(bi.c_str());std::remove(ah.c_str());std::remove(bo.c_str());return false;}
    bool ok=ahlc::images_equal(orig,rec);
    const char* cn=(s.codec_used==ahlc::CODEC_HUFFMAN)?"HUFFMAN":"RLE";
    char rbuf[32];std::snprintf(rbuf,sizeof(rbuf),"%.3fx",s.compressed_file_size>0?(double)s.original_file_size/s.compressed_file_size:0.0);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<"\n";
    std::cout<<"       RLE="<<s.rle_candidate_size<<"B  Huffman="<<s.huffman_candidate_size<<"B"
             <<"  Selected="<<cn<<"  Ratio="<<rbuf<<"  Verify="<<(ok?"PASS":"FAIL")<<"\n";
    std::remove(bi.c_str());std::remove(ah.c_str());std::remove(bo.c_str());
    return ok;
}
static bool run_image_adaptive_test()
{
    std::cout<<"AHLC Adaptive Image Tests\n-------------------------\n";
    bool all=true;
    all&=image_adaptive_rt("Uniform image (all green)",make_image(32,32,[](uint32_t,uint32_t,uint8_t&B,uint8_t&G,uint8_t&R){B=0;G=200;R=0;}));
    all&=image_adaptive_rt("Gradient (structured)",make_image(32,32,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){uint8_t v=static_cast<uint8_t>((r+c)*4);B=v;G=v;R=v;}));
    all&=image_adaptive_rt("Checkerboard pattern",make_image(32,32,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){bool w=(r+c)%2==0;B=w?255:0;G=w?255:0;R=w?255:0;}));
    all&=image_adaptive_rt("Color blocks (4 quadrants)",make_image(32,32,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){bool t=r<16,l=c<16;if(t&&l){B=0;G=0;R=255;}if(t&&!l){B=0;G=255;R=0;}if(!t&&l){B=255;G=0;R=0;}if(!t&&!l){B=255;G=255;R=255;}}));
    all&=image_adaptive_rt("High-entropy (sequential bytes)",make_image(32,32,[](uint32_t r,uint32_t c,uint8_t&B,uint8_t&G,uint8_t&R){uint8_t v=static_cast<uint8_t>((r*32+c)&0xFF);B=v;G=static_cast<uint8_t>(v+85);R=static_cast<uint8_t>(v+170);}));
    std::cout<<"-------------------------\nOverall: "<<(all?"ALL PASS":"SOME FAILED")<<"\n";
    return all;
}

// ================================================================
// audio-test
// ================================================================
static ahlc::AudioBuffer make_audio_mono16(uint32_t sr,const std::vector<int16_t>& s)
{
    ahlc::AudioBuffer a;a.format=1;a.channels=1;a.sample_rate=sr;a.bits_per_sample=16;
    a.total_samples=s.size();a.pcm.resize(s.size()*2);
    for(size_t i=0;i<s.size();++i){a.pcm[i*2]=static_cast<uint8_t>(s[i]&0xFF);a.pcm[i*2+1]=static_cast<uint8_t>((s[i]>>8)&0xFF);}
    return a;
}
static ahlc::AudioBuffer make_audio_stereo16(uint32_t sr,const std::vector<int16_t>& lr)
{
    ahlc::AudioBuffer a;a.format=1;a.channels=2;a.sample_rate=sr;a.bits_per_sample=16;
    a.total_samples=lr.size()/2;a.pcm.resize(lr.size()*2);
    for(size_t i=0;i<lr.size();++i){a.pcm[i*2]=static_cast<uint8_t>(lr[i]&0xFF);a.pcm[i*2+1]=static_cast<uint8_t>((lr[i]>>8)&0xFF);}
    return a;
}
static bool audio_rt(const std::string& name, const ahlc::AudioBuffer& orig)
{
    const std::string wi="__au_i.wav",ah="__au.ahmc",wo="__au_o.wav";
    if(!ahlc::write_wav(wi,orig)){std::cout<<"  [FAIL] "<<name<<"\n";return false;}
    ahlc::AudioCompressStats s;
    if(!ahlc::compress_audio(wi,ah,s)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());return false;}
    if(!ahlc::decompress_audio(ah,wo)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());std::remove(ah.c_str());return false;}
    ahlc::AudioBuffer rec;if(!ahlc::read_wav(wo,rec)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());std::remove(ah.c_str());std::remove(wo.c_str());return false;}
    bool ok=ahlc::audios_equal(orig,rec);
    const char* cn=(s.codec_used==ahlc::AUDIO_CODEC_HUFFMAN)?"HUFFMAN":"RLE";
    char buf[32];std::snprintf(buf,sizeof(buf),"%.2fx",s.compressed_file_size>0?(double)s.original_file_size/s.compressed_file_size:0.0);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<" | "<<orig.channels<<"ch "<<orig.bits_per_sample<<"bit codec="<<cn<<" ratio="<<buf<<"\n";
    std::remove(wi.c_str());std::remove(ah.c_str());std::remove(wo.c_str());
    return ok;
}
static bool run_audio_test()
{
    std::cout<<"AHLC Audio Round-Trip Tests\n---------------------------\n";
    bool all=true;
    const uint32_t SR=44100;const int N=1000;
    {std::vector<int16_t> s(N,5000);all&=audio_rt("Constant signal (5000)",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);for(int i=0;i<N;++i)s[i]=static_cast<int16_t>(20000.0*std::sin(2.0*PI*440.0*i/SR));all&=audio_rt("Sine 440 Hz (mono)",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);for(int i=0;i<N;++i)s[i]=static_cast<int16_t>(20000.0*std::sin(2.0*PI*60.0*i/SR));all&=audio_rt("Sine 60 Hz (low freq)",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);for(int i=0;i<N;++i)s[i]=static_cast<int16_t>(i%32768);all&=audio_rt("Ramp signal",make_audio_mono16(SR,s));}
    {std::vector<int16_t> lr(N*2);for(int i=0;i<N;++i){lr[i*2]=static_cast<int16_t>(15000.0*std::sin(2.0*PI*440.0*i/SR));lr[i*2+1]=static_cast<int16_t>(15000.0*std::sin(2.0*PI*880.0*i/SR));}all&=audio_rt("Stereo sine (440/880 Hz)",make_audio_stereo16(SR,lr));}
    {std::vector<int16_t> s(N);int16_t v=0;for(int i=0;i<N;++i){s[i]=v;v=static_cast<int16_t>(v*1103515245+12345);}all&=audio_rt("Pseudo-random samples",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N,0);all&=audio_rt("Silence (all zeros)",make_audio_mono16(SR,s));}
    std::cout<<"---------------------------\nOverall: "<<(all?"ALL PASS":"SOME FAILED")<<"\n";
    return all;
}

// ================================================================
// audio-adaptive-test
// ================================================================
static bool audio_adaptive_rt(const std::string& name, const ahlc::AudioBuffer& orig)
{
    const std::string wi="__aua_i.wav",ah="__aua.ahmc",wo="__aua_o.wav";
    if(!ahlc::write_wav(wi,orig)){std::cout<<"  [FAIL] "<<name<<"\n";return false;}
    ahlc::AudioCompressStats s;
    if(!ahlc::compress_audio(wi,ah,s)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());return false;}
    if(!ahlc::decompress_audio(ah,wo)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());std::remove(ah.c_str());return false;}
    ahlc::AudioBuffer rec;
    if(!ahlc::read_wav(wo,rec)){std::cout<<"  [FAIL] "<<name<<"\n";std::remove(wi.c_str());std::remove(ah.c_str());std::remove(wo.c_str());return false;}
    bool ok=ahlc::audios_equal(orig,rec);
    const char* cn=(s.codec_used==ahlc::AUDIO_CODEC_HUFFMAN)?"HUFFMAN":"RLE";
    char rbuf[32];std::snprintf(rbuf,sizeof(rbuf),"%.3fx",s.compressed_file_size>0?(double)s.original_file_size/s.compressed_file_size:0.0);
    std::cout<<"  ["<<(ok?"PASS":"FAIL")<<"] "<<name<<"\n";
    std::cout<<"       RLE="<<s.rle_candidate_size<<"B  Huffman="<<s.huffman_candidate_size<<"B"
             <<"  Selected="<<cn<<"  Ratio="<<rbuf<<"  Verify="<<(ok?"PASS":"FAIL")<<"\n";
    std::remove(wi.c_str());std::remove(ah.c_str());std::remove(wo.c_str());
    return ok;
}
static bool run_audio_adaptive_test()
{
    std::cout<<"AHLC Adaptive Audio Tests\n-------------------------\n";
    bool all=true;
    const uint32_t SR=44100; const int N=2000;
    {std::vector<int16_t> s(N,0);all&=audio_adaptive_rt("Silence (all zeros)",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N,8000);all&=audio_adaptive_rt("Constant signal (8000)",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);for(int i=0;i<N;++i)s[i]=static_cast<int16_t>(20000.0*std::sin(2.0*PI*440.0*i/SR));all&=audio_adaptive_rt("Sine 440 Hz",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);for(int i=0;i<N;++i)s[i]=static_cast<int16_t>(i);all&=audio_adaptive_rt("Ramp signal",make_audio_mono16(SR,s));}
    {std::vector<int16_t> s(N);int16_t v=1234;for(int i=0;i<N;++i){s[i]=v;v=static_cast<int16_t>(static_cast<int64_t>(v)*6364136223846793005LL+1442695040888963407LL);}all&=audio_adaptive_rt("Pseudo-random samples",make_audio_mono16(SR,s));}
    {std::vector<int16_t> lr(N*2);for(int i=0;i<N;++i){lr[i*2]=static_cast<int16_t>(15000.0*std::sin(2.0*PI*440.0*i/SR));lr[i*2+1]=static_cast<int16_t>(15000.0*std::sin(2.0*PI*880.0*i/SR));}all&=audio_adaptive_rt("Stereo sine (440/880 Hz)",make_audio_stereo16(SR,lr));}
    std::cout<<"-------------------------\nOverall: "<<(all?"ALL PASS":"SOME FAILED")<<"\n";
    return all;
}

// ================================================================
// RLE vs Huffman comparison
// ================================================================
static void run_entropy_compare(const std::string& label, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> rle_out,huff_out;
    bool rle_ok=ahlc::rle_compress_mem(data,rle_out);
    bool huff_ok=ahlc::huffman_compress_mem(data,huff_out);
    double rle_ratio=(rle_ok&&rle_out.size()>0)?(double)data.size()/rle_out.size():0.0;
    double huff_ratio=(huff_ok&&huff_out.size()>0)?(double)data.size()/huff_out.size():0.0;
    std::cout<<"  "<<label<<"\n";
    std::cout<<"    Original  : "<<data.size()<<" B\n";
    std::cout<<"    RLE       : "<<(rle_ok?std::to_string(static_cast<uint64_t>(rle_out.size())):std::string("ERR"))<<" B  (ratio "<<std::fixed<<std::setprecision(3)<<rle_ratio<<"x)\n";
    std::cout<<"    Huffman   : "<<(huff_ok?std::to_string(static_cast<uint64_t>(huff_out.size())):std::string("ERR"))<<" B  (ratio "<<huff_ratio<<"x)\n";
    std::cout<<"    Winner    : "<<((rle_out.size()<=huff_out.size())?"RLE":"Huffman")<<"\n";
}

// ================================================================
// JSON output helpers (used by --json mode)
// ================================================================
static void print_image_json(const ahlc::ImageCompressStats& s, bool verified)
{
    double ratio = (s.compressed_file_size > 0)
        ? (double)s.original_file_size / s.compressed_file_size : 0.0;
    double saved = (s.original_file_size > 0)
        ? (1.0 - (double)s.compressed_file_size / s.original_file_size) * 100.0 : 0.0;
    const char* codec = (s.codec_used == ahlc::CODEC_HUFFMAN) ? "huffman" : "rle";
    std::printf("{\n");
    std::printf("  \"ok\": true,\n");
    std::printf("  \"media_type\": \"image\",\n");
    std::printf("  \"width\": %u,\n", s.width);
    std::printf("  \"height\": %u,\n", s.height);
    std::printf("  \"bpp\": %u,\n", (unsigned)s.bpp);
    std::printf("  \"original_size\": %llu,\n", (unsigned long long)s.original_file_size);
    std::printf("  \"compressed_size\": %llu,\n", (unsigned long long)s.compressed_file_size);
    std::printf("  \"compression_ratio\": %.6f,\n", ratio);
    std::printf("  \"space_saved_percent\": %.4f,\n", saved);
    std::printf("  \"predictor\": \"left\",\n");
    std::printf("  \"rle_size\": %llu,\n", (unsigned long long)s.rle_candidate_size);
    std::printf("  \"huffman_size\": %llu,\n", (unsigned long long)s.huffman_candidate_size);
    std::printf("  \"selected_codec\": \"%s\",\n", codec);
    std::printf("  \"original_entropy\": %.6f,\n", s.entropy_before);
    std::printf("  \"residual_entropy\": %.6f,\n", s.entropy_after);
    std::printf("  \"original_pixel_bytes\": %llu,\n", (unsigned long long)s.original_pixel_bytes);
    std::printf("  \"verification\": %s\n", verified ? "true" : "false");
    std::printf("}\n");
}

static void print_eabps_json(const ahlc::EABPSCompressStats& s, bool verified)
{
    double ratio = (s.compressed_file_size > 0)
        ? (double)s.original_file_size / s.compressed_file_size : 0.0;
    double saved = (s.original_file_size > 0)
        ? (1.0 - (double)s.compressed_file_size / s.original_file_size) * 100.0 : 0.0;
    std::printf("{\n");
    std::printf("  \"ok\": true,\n");
    std::printf("  \"algorithm\": \"EABPS\",\n");
    std::printf("  \"media_type\": \"image\",\n");
    std::printf("  \"width\": %u,\n", s.width);
    std::printf("  \"height\": %u,\n", s.height);
    std::printf("  \"bpp\": %u,\n", (unsigned)s.bpp);
    std::printf("  \"block_size\": %u,\n", (unsigned)s.block_size);
    std::printf("  \"num_blocks_x\": %u,\n", s.num_blocks_x);
    std::printf("  \"num_blocks_y\": %u,\n", s.num_blocks_y);
    std::printf("  \"total_blocks\": %u,\n", s.total_blocks);
    std::printf("  \"original_size\": %llu,\n", (unsigned long long)s.original_file_size);
    std::printf("  \"compressed_size\": %llu,\n", (unsigned long long)s.compressed_file_size);
    std::printf("  \"encoded_payload_size\": %llu,\n", (unsigned long long)s.encoded_payload_size);
    std::printf("  \"predictor_metadata_size\": %llu,\n", (unsigned long long)s.predictor_metadata_size);
    std::printf("  \"container_overhead\": %llu,\n", (unsigned long long)s.container_overhead);
    std::printf("  \"compression_ratio\": %.6f,\n", ratio);
    std::printf("  \"space_saved_percent\": %.4f,\n", saved);
    std::printf("  \"predictor\": \"eabps\",\n");
    std::printf("  \"count_none\": %u,\n", s.count_none);
    std::printf("  \"count_left\": %u,\n", s.count_left);
    std::printf("  \"count_up\": %u,\n", s.count_up);
    std::printf("  \"count_rle\": %u,\n", s.count_rle);
    std::printf("  \"count_huffman\": %u,\n", s.count_huffman);
    std::printf("  \"count_raw\": %u,\n", s.count_raw);
    std::printf("  \"original_entropy\": %.6f,\n", s.entropy_before);
    std::printf("  \"residual_entropy\": %.6f,\n", s.entropy_after);
    std::printf("  \"original_pixel_bytes\": %llu,\n", (unsigned long long)s.original_pixel_bytes);
    std::printf("  \"compression_time_ms\": %.3f,\n", s.compression_time_ms);
    std::printf("  \"verification\": %s\n", verified ? "true" : "false");
    std::printf("}\n");
}

static void print_audio_json(const ahlc::AudioCompressStats& s, bool verified)
{
    double ratio = (s.compressed_file_size > 0)
        ? (double)s.original_file_size / s.compressed_file_size : 0.0;
    double saved = (s.original_file_size > 0)
        ? (1.0 - (double)s.compressed_file_size / s.original_file_size) * 100.0 : 0.0;
    const char* codec = (s.codec_used == ahlc::AUDIO_CODEC_HUFFMAN) ? "huffman" : "rle";
    double duration = (s.sample_rate > 0)
        ? (double)s.total_samples / s.sample_rate : 0.0;
    std::printf("{\n");
    std::printf("  \"ok\": true,\n");
    std::printf("  \"media_type\": \"audio\",\n");
    std::printf("  \"channels\": %u,\n", (unsigned)s.channels);
    std::printf("  \"sample_rate\": %u,\n", s.sample_rate);
    std::printf("  \"bits_per_sample\": %u,\n", (unsigned)s.bits_per_sample);
    std::printf("  \"total_samples\": %llu,\n", (unsigned long long)s.total_samples);
    std::printf("  \"duration_seconds\": %.4f,\n", duration);
    std::printf("  \"original_size\": %llu,\n", (unsigned long long)s.original_file_size);
    std::printf("  \"compressed_size\": %llu,\n", (unsigned long long)s.compressed_file_size);
    std::printf("  \"compression_ratio\": %.6f,\n", ratio);
    std::printf("  \"space_saved_percent\": %.4f,\n", saved);
    std::printf("  \"predictor\": \"delta\",\n");
    std::printf("  \"rle_size\": %llu,\n", (unsigned long long)s.rle_candidate_size);
    std::printf("  \"huffman_size\": %llu,\n", (unsigned long long)s.huffman_candidate_size);
    std::printf("  \"selected_codec\": \"%s\",\n", codec);
    std::printf("  \"original_entropy\": %.6f,\n", s.entropy_before);
    std::printf("  \"residual_entropy\": %.6f,\n", s.entropy_after);
    std::printf("  \"original_pcm_bytes\": %llu,\n", (unsigned long long)s.original_pcm_bytes);
    std::printf("  \"verification\": %s\n", verified ? "true" : "false");
    std::printf("}\n");
}

// ================================================================
// Usage
// ================================================================
static void print_usage()
{
    std::cout
        <<"Usage:\n"
        <<"  ahmc analyze <file>                          Analyze any file\n"
        <<"  ahmc rle-compress <in> <out>                 RLE compress\n"
        <<"  ahmc rle-decompress <in> <out>               RLE decompress\n"
        <<"  ahmc rle-verify <orig> <rec>                 Verify RLE\n"
        <<"  ahmc rle-test                                Run RLE tests\n"
        <<"  ahmc bitio-test                              Run Bit I/O tests\n"
        <<"  ahmc image-analyze <in.bmp> [--json]         Image analysis\n"
        <<"  ahmc image-predict <in.bmp>                  Spatial predictor stats\n"
        <<"  ahmc image-compress <in.bmp> <out.ahmc> [--json]  Adaptive image compress\n"
        <<"  ahmc image-decompress <in.ahmc> <out.bmp>    Decompress image\n"
        <<"  ahmc image-verify <orig.bmp> <rec.bmp>       Verify image\n"
        <<"  ahmc image-test                              Run image tests\n"
        <<"  ahmc image-adaptive-test                     Run adaptive image tests\n"
        <<"  ahmc image-eabps-compress <in.bmp> <out.ahmc> [--block-size N] [--json]  EABPS compress\n"
        <<"  ahmc image-eabps-decompress <in.ahmc> <out.bmp> EABPS decompress\n"
        <<"  ahmc image-eabps-test                        Run EABPS test suite\n"
        <<"  ahmc image-eabps-benchmark                   Run EABPS benchmark against baseline\n"
        <<"  ahmc audio-analyze <in.wav> [--json]         Audio analysis\n"
        <<"  ahmc audio-predict <in.wav>                  Delta predictor stats\n"
        <<"  ahmc audio-compress <in.wav> <out.ahmc> [--json]  Adaptive audio compress\n"
        <<"  ahmc audio-decompress <in.ahmc> <out.wav>    Decompress audio\n"
        <<"  ahmc audio-verify <orig.wav> <rec.wav>       Verify audio\n"
        <<"  ahmc audio-test                              Run audio tests\n"
        <<"  ahmc audio-adaptive-test                     Run adaptive audio tests\n"
        <<"  ahmc huffman-compress <in> <out>             Huffman compress\n"
        <<"  ahmc huffman-decompress <in> <out>           Huffman decompress\n"
        <<"  ahmc huffman-verify <orig> <rec>             Verify Huffman\n"
        <<"  ahmc huffman-test                            Run Huffman tests\n"
        <<"  ahmc entropy-compare <file>                  Compare RLE vs Huffman\n"
        <<"  ahmc ffmpeg-test                             Verify FFmpeg integration\n"
        <<"  ahmc format-analyze <file>                   Analyze media format\n"
        <<"  ahmc format-test                             Run format detection tests\n";
}

// ================================================================
// main
// ================================================================
int main(int argc, char* argv[])
{
    // Check json mode early (before any output)
    bool json_mode = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--json") { json_mode = true; break; }

    if (!json_mode)
        std::cout << "AHLC Compression Engine\nVersion: 0.1\nPhase: Adaptive Entropy Coding\n\n";

    if (argc < 2) { if (!json_mode) print_usage(); return 0; }
    std::string cmd = argv[1];

    // ---- Generic analyzer ----
    if (cmd=="analyze"){if(argc<3){std::cerr<<"Error: needs file.\n";return 1;}ahlc::AnalysisResult r;if(!ahlc::analyze_file(argv[2],r))return 1;ahlc::print_report(r);return 0;}

    // ---- RLE ----
    if(cmd=="rle-compress"){if(argc<4){std::cerr<<"Error: needs <in> <out>.\n";return 1;}ahlc::RleStats s;if(!ahlc::rle_compress(argv[2],argv[3],s))return 1;std::cout<<"--------------------------------------------\n RLE Compression\n--------------------------------------------\n";ahlc::print_rle_stats(s);std::cout<<"--------------------------------------------\n";return 0;}
    if(cmd=="rle-decompress"){if(argc<4){std::cerr<<"Error: needs <in> <out>.\n";return 1;}if(!ahlc::rle_decompress(argv[2],argv[3]))return 1;std::cout<<"Done: "<<argv[3]<<"\n";return 0;}
    if(cmd=="rle-verify"){if(argc<4){std::cerr<<"Error: needs <orig> <rec>.\n";return 1;}bool ok=ahlc::files_identical(argv[2],argv[3]);std::cout<<"Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:1;}
    if(cmd=="rle-test")  return run_rle_test()  ?0:1;
    if(cmd=="bitio-test")return run_bitio_test()?0:1;

    // ---- Image ----
    if (cmd=="image-analyze") {
        if(argc<3){std::cerr<<"Error: needs <in.bmp>.\n";return 1;}
        ahlc::BMPImage img;
        if(!ahlc::read_bmp(argv[2],img)){
            if(json_mode) std::cout<<"{\"ok\":false,\"error\":\"Cannot read BMP\"}\n";
            return 1;
        }
        if(json_mode){
            double ent=ahlc::img_entropy(img.pixels);
            bool seen[256]={};for(uint8_t b:img.pixels)seen[b]=true;
            int uniq=0;for(bool v:seen)if(v)++uniq;
            std::printf("{\n");
            std::printf("  \"ok\": true,\n");
            std::printf("  \"media_type\": \"image\",\n");
            std::printf("  \"filename\": %s,\n", jstr(argv[2]).c_str());
            std::printf("  \"width\": %u,\n", img.width);
            std::printf("  \"height\": %u,\n", img.height);
            std::printf("  \"bpp\": %u,\n", (unsigned)img.bpp);
            std::printf("  \"total_pixel_bytes\": %llu,\n", (unsigned long long)img.total_bytes());
            std::printf("  \"unique_bytes\": %d,\n", uniq);
            std::printf("  \"entropy\": %.6f\n", ent);
            std::printf("}\n");
        } else {
            ahlc::print_image_analysis(argv[2],img);
        }
        return 0;
    }
    if (cmd=="image-predict"){if(argc<3){std::cerr<<"Error: needs <in.bmp>.\n";return 1;}ahlc::BMPImage img;if(!ahlc::read_bmp(argv[2],img))return 1;auto res=ahlc::encode_spatial_predictor(img);ahlc::print_predictor_analysis(img,res);return 0;}
    if (cmd=="image-compress") {
        if(argc<4){std::cerr<<"Error: needs <in.bmp> <out.ahmc>.\n";return 1;}
        bool jm = has_flag(argc,argv,"--json");
        ahlc::ImageCompressStats s;
        if(!ahlc::compress_image(argv[2],argv[3],s)){
            if(jm) std::cout<<"{\"ok\":false,\"error\":\"Compression failed\"}\n";
            return 1;
        }
        if(jm){
            // Verify: decompress to temp, compare
            std::string tmp = std::string(argv[3]) + "_tmp_verify.bmp";
            bool verified = false;
            if(ahlc::decompress_image(argv[3],tmp)){
                ahlc::BMPImage orig,rec;
                if(ahlc::read_bmp(argv[2],orig) && ahlc::read_bmp(tmp,rec))
                    verified = ahlc::images_equal(orig,rec);
                std::remove(tmp.c_str());
            }
            print_image_json(s, verified);
        } else {
            ahlc::print_image_compress_stats(argv[2],argv[3],s);
            // Inline verify
            ahlc::BMPImage orig,rec;
            if(ahlc::read_bmp(argv[2],orig)){
                std::string tmp="__verify_tmp.bmp";
                if(ahlc::decompress_image(argv[3],tmp) && ahlc::read_bmp(tmp,rec)){
                    bool ok=ahlc::images_equal(orig,rec);
                    std::cout<<" Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";
                    std::cout<<"--------------------------------------------\n";
                    std::remove(tmp.c_str());
                }
            }
        }
        return 0;
    }
    if(cmd=="image-decompress"){if(argc<4){std::cerr<<"Error: needs <in.ahmc> <out.bmp>.\n";return 1;}if(!ahlc::decompress_image(argv[2],argv[3]))return 1;std::cout<<"Done: "<<argv[3]<<"\n";return 0;}
    if(cmd=="image-verify"){if(argc<4){std::cerr<<"Error: needs <orig.bmp> <rec.bmp>.\n";return 1;}ahlc::BMPImage a,b;if(!ahlc::read_bmp(argv[2],a))return 1;if(!ahlc::read_bmp(argv[3],b))return 1;bool ok=ahlc::images_equal(a,b);std::cout<<"Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:1;}
    if(cmd=="image-test")         return run_image_test()         ?0:1;
    if(cmd=="image-adaptive-test")return run_image_adaptive_test()?0:1;

    // ---- EABPS ----
    if (cmd == "image-eabps-compress") {
        if (argc < 4) { std::cerr << "Error: needs <in.bmp> <out.ahmc> [--block-size N] [--json].\n"; return 1; }
        bool jm = has_flag(argc, argv, "--json");
        std::string bs_str = get_option_val(argc, argv, "--block-size", "16");
        uint16_t bsize = static_cast<uint16_t>(std::clamp(std::atoi(bs_str.c_str()), 1, 1024));
        ahlc::EABPSCompressStats s;
        if (!ahlc::compress_image_eabps(argv[2], argv[3], s, bsize)) {
            if (jm) std::cout << "{\"ok\":false,\"error\":\"EABPS compression failed\"}\n";
            return 1;
        }
        if (jm) {
            std::string tmp = std::string(argv[3]) + "_tmp_verify.bmp";
            bool verified = false;
            if (ahlc::decompress_image_eabps(argv[3], tmp)) {
                ahlc::BMPImage orig, rec;
                if (ahlc::read_bmp(argv[2], orig) && ahlc::read_bmp(tmp, rec))
                    verified = ahlc::images_equal(orig, rec);
                std::remove(tmp.c_str());
            }
            print_eabps_json(s, verified);
        } else {
            ahlc::print_eabps_stats(argv[2], argv[3], s);
            std::string tmp = "__verify_eabps_tmp.bmp";
            if (ahlc::decompress_image_eabps(argv[3], tmp)) {
                ahlc::BMPImage orig, rec;
                if (ahlc::read_bmp(argv[2], orig) && ahlc::read_bmp(tmp, rec)) {
                    bool ok = ahlc::images_equal(orig, rec);
                    std::cout << " Lossless verification: " << (ok ? "PASS" : "FAIL") << "\n";
                    std::cout << "--------------------------------------------\n";
                }
                std::remove(tmp.c_str());
            }
        }
        return 0;
    }
    if (cmd == "image-eabps-decompress") {
        if (argc < 4) { std::cerr << "Error: needs <in.ahmc> <out.bmp>.\n"; return 1; }
        if (!ahlc::decompress_image_eabps(argv[2], argv[3])) return 1;
        std::cout << "Done: " << argv[3] << "\n";
        return 0;
    }
    if (cmd == "image-eabps-test")      return ahlc::run_eabps_test_suite() ? 0 : 1;
    if (cmd == "image-eabps-benchmark") return ahlc::run_eabps_benchmark()  ? 0 : 1;

    // ---- Audio ----
    if (cmd=="audio-analyze") {
        if(argc<3){std::cerr<<"Error: needs <in.wav>.\n";return 1;}
        ahlc::AudioBuffer audio;
        if(!ahlc::read_wav(argv[2],audio)){
            if(json_mode) std::cout<<"{\"ok\":false,\"error\":\"Cannot read WAV\"}\n";
            return 1;
        }
        if(json_mode){
            double ent=ahlc::audio_entropy(audio.pcm);
            bool seen[256]={};for(uint8_t b:audio.pcm)seen[b]=true;
            int uniq=0;for(bool v:seen)if(v)++uniq;
            double dur=(audio.sample_rate>0)?(double)audio.total_samples/audio.sample_rate:0.0;
            std::printf("{\n");
            std::printf("  \"ok\": true,\n");
            std::printf("  \"media_type\": \"audio\",\n");
            std::printf("  \"filename\": %s,\n", jstr(argv[2]).c_str());
            std::printf("  \"channels\": %u,\n", (unsigned)audio.channels);
            std::printf("  \"sample_rate\": %u,\n", audio.sample_rate);
            std::printf("  \"bits_per_sample\": %u,\n", (unsigned)audio.bits_per_sample);
            std::printf("  \"total_samples\": %llu,\n", (unsigned long long)audio.total_samples);
            std::printf("  \"duration_seconds\": %.4f,\n", dur);
            std::printf("  \"pcm_bytes\": %llu,\n", (unsigned long long)audio.pcm.size());
            std::printf("  \"unique_bytes\": %d,\n", uniq);
            std::printf("  \"entropy\": %.6f\n", ent);
            std::printf("}\n");
        } else {
            ahlc::print_audio_analysis(argv[2],audio);
        }
        return 0;
    }
    if(cmd=="audio-predict"){if(argc<3){std::cerr<<"Error: needs <in.wav>.\n";return 1;}ahlc::AudioBuffer audio;if(!ahlc::read_wav(argv[2],audio))return 1;auto res=ahlc::encode_audio_predictor(audio);ahlc::print_audio_predictor_analysis(audio,res);return 0;}
    if (cmd=="audio-compress") {
        if(argc<4){std::cerr<<"Error: needs <in.wav> <out.ahmc>.\n";return 1;}
        bool jm = has_flag(argc,argv,"--json");
        ahlc::AudioCompressStats s;
        if(!ahlc::compress_audio(argv[2],argv[3],s)){
            if(jm) std::cout<<"{\"ok\":false,\"error\":\"Compression failed\"}\n";
            return 1;
        }
        if(jm){
            std::string tmp = std::string(argv[3]) + "_tmp_verify.wav";
            bool verified = false;
            if(ahlc::decompress_audio(argv[3],tmp)){
                ahlc::AudioBuffer orig,rec;
                if(ahlc::read_wav(argv[2],orig) && ahlc::read_wav(tmp,rec))
                    verified = ahlc::audios_equal(orig,rec);
                std::remove(tmp.c_str());
            }
            print_audio_json(s, verified);
        } else {
            ahlc::print_audio_compress_stats(argv[2],argv[3],s);
            ahlc::AudioBuffer orig,rec;
            if(ahlc::read_wav(argv[2],orig)){
                std::string tmp="__verify_tmp.wav";
                if(ahlc::decompress_audio(argv[3],tmp) && ahlc::read_wav(tmp,rec)){
                    bool ok=ahlc::audios_equal(orig,rec);
                    std::cout<<" Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";
                    std::cout<<"--------------------------------------------\n";
                    std::remove(tmp.c_str());
                }
            }
        }
        return 0;
    }
    if(cmd=="audio-decompress"){if(argc<4){std::cerr<<"Error: needs <in.ahmc> <out.wav>.\n";return 1;}if(!ahlc::decompress_audio(argv[2],argv[3]))return 1;std::cout<<"Done: "<<argv[3]<<"\n";return 0;}
    if(cmd=="audio-verify"){if(argc<4){std::cerr<<"Error: needs <orig.wav> <rec.wav>.\n";return 1;}ahlc::AudioBuffer a,b;if(!ahlc::read_wav(argv[2],a))return 1;if(!ahlc::read_wav(argv[3],b))return 1;bool ok=ahlc::audios_equal(a,b);std::cout<<"Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:1;}
    if(cmd=="audio-test")         return run_audio_test()         ?0:1;
    if(cmd=="audio-adaptive-test")return run_audio_adaptive_test()?0:1;

    // ---- Huffman ----
    if(cmd=="huffman-compress"){if(argc<4){std::cerr<<"Error: needs <in> <out>.\n";return 1;}ahlc::HuffStats s;if(!ahlc::huffman_compress(argv[2],argv[3],s))return 1;ahlc::print_huff_stats(s);return 0;}
    if(cmd=="huffman-decompress"){if(argc<4){std::cerr<<"Error: needs <in> <out>.\n";return 1;}if(!ahlc::huffman_decompress(argv[2],argv[3]))return 1;std::cout<<"Done: "<<argv[3]<<"\n";return 0;}
    if(cmd=="huffman-verify"){if(argc<4){std::cerr<<"Error: needs <orig> <rec>.\n";return 1;}bool ok=ahlc::files_identical(argv[2],argv[3]);std::cout<<"Lossless verification: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:1;}
    if(cmd=="huffman-test")return run_huffman_test()?0:1;

    // ---- Entropy compare ----
    if(cmd=="entropy-compare"){
        if(argc<3){std::cerr<<"Error: needs a file.\n";return 1;}
        std::cout<<"--------------------------------------------\n RLE vs Huffman Comparison\n--------------------------------------------\n";
        ahlc::BMPImage img;if(ahlc::read_bmp(argv[2],img)){auto res=ahlc::encode_spatial_predictor(img);run_entropy_compare("Image residuals",res);run_entropy_compare("Image raw pixels",img.pixels);return 0;}
        ahlc::AudioBuffer audio;if(ahlc::read_wav(argv[2],audio)){auto res=ahlc::encode_audio_predictor(audio);run_entropy_compare("Audio residuals",res);run_entropy_compare("Audio raw PCM",audio.pcm);return 0;}
        std::vector<uint8_t> raw;if(read_bytes(argv[2],raw)){run_entropy_compare(std::string("File: ")+argv[2],raw);return 0;}
        std::cerr<<"Error: cannot read "<<argv[2]<<"\n";return 1;
    }

    // ---- FFmpeg integration test ----
    if (cmd == "ffmpeg-test") {
        std::cout << "--------------------------------------------\n";
        std::cout << " AHLC FFmpeg Integration Test\n";
        std::cout << "--------------------------------------------\n";

        // Build-time version constants (from headers)
        std::cout << " Compiled against:\n";
        std::printf("   libavutil    version : %d.%d.%d\n",
            LIBAVUTIL_VERSION_MAJOR,
            LIBAVUTIL_VERSION_MINOR,
            LIBAVUTIL_VERSION_MICRO);
        std::printf("   libavcodec   version : %d.%d.%d\n",
            LIBAVCODEC_VERSION_MAJOR,
            LIBAVCODEC_VERSION_MINOR,
            LIBAVCODEC_VERSION_MICRO);
        std::printf("   libavformat  version : %d.%d.%d\n",
            LIBAVFORMAT_VERSION_MAJOR,
            LIBAVFORMAT_VERSION_MINOR,
            LIBAVFORMAT_VERSION_MICRO);
        std::printf("   libswscale   version : %d.%d.%d\n",
            LIBSWSCALE_VERSION_MAJOR,
            LIBSWSCALE_VERSION_MINOR,
            LIBSWSCALE_VERSION_MICRO);
        std::printf("   libswresample version: %d.%d.%d\n",
            LIBSWRESAMPLE_VERSION_MAJOR,
            LIBSWRESAMPLE_VERSION_MINOR,
            LIBSWRESAMPLE_VERSION_MICRO);

        // Runtime version strings (from linked library)
        std::cout << "\n Linked library versions:\n";
        std::printf("   avutil   : %s\n", av_version_info());
        std::printf("   avutil   : %u (runtime)\n", avutil_version());
        std::printf("   avcodec  : %u (runtime)\n", avcodec_version());
        std::printf("   avformat : %u (runtime)\n", avformat_version());
        std::printf("   swscale  : %u (runtime)\n", swscale_version());
        std::printf("   swresample:%u (runtime)\n", swresample_version());

        std::cout << "\n FFmpeg license : " << avutil_license() << "\n";
        std::cout << "\n Status: FFmpeg headers included and libraries linked correctly.\n";
        std::cout << "--------------------------------------------\n";
        std::cout << " Result: PASS\n";
        std::cout << "--------------------------------------------\n";
        return 0;
    }

    // ---- Format Analysis ----
    if (cmd == "format-analyze") {
        if(argc < 3){std::cerr<<"Error: needs a file.\n";return 1;}
        ahlc::media::FormatInfo info = ahlc::media::detect_format(argv[2]);
        ahlc::media::print_format_analysis(argv[2], info);
        return info.ok ? 0 : 1;
    }
    if (cmd == "format-test") {
        std::cout << "AHLC Format Detection Test\n--------------------------\n";
        bool ok = true;
        
        // 1. Create a tiny synthetic BMP
        const std::string tmp_bmp = "format_test_tmp.bmp";
        std::vector<uint8_t> bmp_data = {
            0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00
        };
        write_bytes(tmp_bmp, bmp_data);
        
        auto info = ahlc::media::detect_format(tmp_bmp);
        std::remove(tmp_bmp.c_str());
        
        if (!info.ok || info.media_type != ahlc::media::MediaType::Image || info.format != ahlc::media::MediaFormat::BMP) {
            std::cout << "BMP detection: FAIL\n";
            ok = false;
        } else {
            std::cout << "BMP detection: PASS\n";
        }
        
        std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
        return ok ? 0 : 1;
    }

    std::cerr<<"Error: Unknown command '"<<cmd<<"'.\n";print_usage();return 1;
}
