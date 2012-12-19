// imagew-util.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This file is mainly for portability wrappers, and any code that
// may require unusual header files (malloc.h, strsafe.h).

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IW_WINDOWS
#include <malloc.h>
#endif
#include <stdarg.h>
#include <time.h>

#include "imagew-internals.h"
#ifdef IW_WINDOWS
#include <strsafe.h>
#endif


void* iwpvt_default_malloc(void *userdata, unsigned int flags, size_t n)
{
	if(flags & IW_MALLOCFLAG_ZEROMEM) {
		return calloc(n,1);
	}
	return malloc(n);
}

void iwpvt_default_free(void *userdata, void *mem)
{
	free(mem);
}

IW_IMPL(void*) iw_malloc_ex(struct iw_context *ctx, unsigned int flags, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		if(!(flags&IW_MALLOCFLAG_NOERRORS))
			iw_set_error(ctx,"Out of memory");
		return NULL;
	}

	mem = (*ctx->mallocfn)(ctx->userdata,flags,n);

	if(!mem) {
		if(!(flags&IW_MALLOCFLAG_NOERRORS))
			iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	return mem;
}

IW_IMPL(void*) iw_malloc(struct iw_context *ctx, size_t n)
{
	return iw_malloc_ex(ctx,0,n);
}

IW_IMPL(void*) iw_mallocz(struct iw_context *ctx, size_t n)
{
	return iw_malloc_ex(ctx,IW_MALLOCFLAG_ZEROMEM,n);
}

// Allocate a large block of memory, presumably for image data.
// Use this if integer overflow is a possibility when multiplying
// two factors together.
IW_IMPL(void*) iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2)
{
	if(n1 > ctx->max_malloc/n2) {
		iw_set_error(ctx,"Image too large to process");
		return NULL;
	}
	return iw_malloc_ex(ctx,0,n1*n2);
}

// Emulate realloc using malloc, by always allocating a new memory block.
static void* emulated_realloc(struct iw_context *ctx, unsigned int flags,
	void *oldmem, size_t oldmem_size, size_t newmem_size)
{
	void *newmem;

	newmem = (*ctx->mallocfn)(ctx->userdata,flags,newmem_size);
	if(oldmem && newmem) {
		if(oldmem_size<newmem_size)
			memcpy(newmem,oldmem,oldmem_size);
		else
			memcpy(newmem,oldmem,newmem_size);
	}
	if(oldmem) {
		// Our realloc functions always free the old memory, even on failure.
		(*ctx->freefn)(ctx->userdata,oldmem);
	}
	return newmem;
}

IW_IMPL(void*) iw_realloc_ex(struct iw_context *ctx, unsigned int flags,
	void *oldmem, size_t oldmem_size, size_t newmem_size)
{
	void *mem;

	if(!oldmem) {
		return iw_malloc_ex(ctx,flags,newmem_size);
	}

	if(newmem_size>ctx->max_malloc) {
		if(!(flags&IW_MALLOCFLAG_NOERRORS))
			iw_set_error(ctx,"Out of memory");
		return NULL;
	}

	mem = emulated_realloc(ctx,flags,oldmem,oldmem_size,newmem_size);

	if(!mem) {
		if(!(flags&IW_MALLOCFLAG_NOERRORS))
			iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	return mem;
}

IW_IMPL(void*) iw_realloc(struct iw_context *ctx, void *oldmem,
	size_t oldmem_size, size_t newmem_size)
{
	return iw_realloc_ex(ctx,0,oldmem,oldmem_size,newmem_size);
}

IW_IMPL(void) iw_free(struct iw_context *ctx, void *mem)
{
	if(!mem) return;
	// Note that this function can be used to free the ctx struct itself,
	// so we're not allowed to use ctx after freeing the memory.
	(*ctx->freefn)(ctx->userdata,mem);
}

IW_IMPL(void) iw_strlcpy(char *dst, const char *src, size_t dstlen)
{
	size_t n;
	n = strlen(src);
	if(n>dstlen-1) n=dstlen-1;
	memcpy(dst,src,n);
	dst[n]='\0';
}

IW_IMPL(void) iw_vsnprintf(char *buf, size_t buflen, const char *fmt, va_list ap)
{
#ifdef IW_WINDOWS
	StringCchVPrintfA(buf,buflen,fmt,ap);
#else
	vsnprintf(buf,buflen,fmt,ap);
	buf[buflen-1]='\0';
#endif
}

IW_IMPL(void) iw_snprintf(char *buf, size_t buflen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iw_vsnprintf(buf,buflen,fmt,ap);
	va_end(ap);
}

IW_IMPL(int) iw_stricmp(const char *s1, const char *s2)
{
#ifdef IW_WINDOWS
	return _stricmp(s1,s2);
#else
	return strcasecmp(s1,s2);
#endif
}

IW_IMPL(void) iw_zeromem(void *mem, size_t n)
{
	memset(mem,0,n);
}

////////////////////////////////////////////
// A simple carry-with-multiply pseudorandom number generator (PRNG).

struct iw_prng {
	iw_uint32 multiply;
	iw_uint32 carry;
};

struct iw_prng *iwpvt_prng_create(struct iw_context *ctx)
{
	struct iw_prng *prng;
	prng = (struct iw_prng*)iw_mallocz(ctx,sizeof(struct iw_prng));
	if(!prng) return NULL;
	return prng;
}

void iwpvt_prng_destroy(struct iw_context *ctx, struct iw_prng *prng)
{
	if(prng) iw_free(ctx,(void*)prng);
}

void iwpvt_prng_set_random_seed(struct iw_prng *prng, int s)
{
	prng->multiply = ((iw_uint32)0x03333333) + s;
	prng->carry    = ((iw_uint32)0x05555555) + s;
}

iw_uint32 iwpvt_prng_rand(struct iw_prng *prng)
{
	iw_uint64 x;
	x = ((iw_uint64)0xfff0bf23) * prng->multiply + prng->carry;
	prng->carry = (iw_uint32)(x>>32);
	prng->multiply = 0xffffffff - (0xffffffff & x);
	return prng->multiply;
}

////////////////////////////////////////////

int iwpvt_util_randomize(struct iw_prng *prng)
{
	int s;
	s = (int)time(NULL);
	iwpvt_prng_set_random_seed(prng, s);
	return s;
}

IW_IMPL(int) iw_file_to_memory(struct iw_context *ctx, struct iw_iodescr *iodescr,
  void **pmem, iw_int64 *psize)
{
	int ret;
	size_t bytesread;

	*pmem=NULL;
	*psize=0;

	if(!iodescr->getfilesize_fn) return 0;

	ret = (*iodescr->getfilesize_fn)(ctx,iodescr,psize);
	if(!ret) return 0;

	*pmem = iw_malloc(ctx,(size_t)*psize);

	ret = (*iodescr->read_fn)(ctx,iodescr,*pmem,(size_t)*psize,&bytesread);
	if(!ret) return 0;
	if((iw_int64)bytesread != *psize) return 0;
	return 1;
}

struct iw_utf8cvt_struct {
	char *dst;
	int dstlen;
	int dp;
};

static void utf8cvt_emitoctet(struct iw_utf8cvt_struct *s, unsigned char c)
{
	if(s->dp > s->dstlen-2) return;
	s->dst[s->dp] = (char)c;
	s->dp++;
}

// Map Unicode characters to ASCII substitutions.
// Not used for codepoints <=127.
static void utf8cvt_emitunichar(struct iw_utf8cvt_struct *s, unsigned int c)
{
	int i;
	int pos;
	struct charmap_struct {
		unsigned int code;
		const char *s;
	};
	static const struct charmap_struct chartable[] = {
	 {0, "?" }, // Default character
	 {0x00a9, "(c)" },
	 {0x00d7, "x" }, // multiplication sign
	 {0x2013, "-" }, // en dash
	 {0x2018, "'" }, // left single quote
	 {0x2019, "'" }, // right single quote
	 {0x201c, "\"" }, // left double quote
	 {0x201d, "\"" }, // right double quote
	 {0x2192, "->" },
	 {0xfeff, "" }, // zero-width no-break space
	 {0, NULL}
	};

	// Try to find the codepoint in the table.
	pos = 0;
	for(i=1;chartable[i].code;i++) {
		if(c==chartable[i].code) {
			pos=i;
			break;
		}
	}

	// Write out the ASCII translation of this code.
	for(i=0;chartable[pos].s[i];i++) {
		utf8cvt_emitoctet(s,(unsigned char)chartable[pos].s[i]);
	}
}

// This UTF-8 converter is intended to be safe to use with malformed data, but
// it may not handle it in the best possible way. It mostly just skips over it.
IW_IMPL(void) iw_utf8_to_ascii(const char *src, char *dst, int dstlen)
{
	struct iw_utf8cvt_struct s;
	int sp;
	unsigned char c;
	unsigned int pending_char;
	int bytes_expected;

	s.dst = dst;
	s.dstlen = dstlen;
	s.dp = 0;
	pending_char=0;
	bytes_expected=0;

	for(sp=0;src[sp];sp++) {
		c = (unsigned char)src[sp];
		if(c<128) { // Only byte of a 1-byte sequence
			utf8cvt_emitoctet(&s,c);
			bytes_expected=0;
		}
		else if(c<0xc0) { // Continuation byte
			if(bytes_expected>0) {
				pending_char = (pending_char<<6)|(c&0x3f);
				bytes_expected--;
				if(bytes_expected<1) {
					utf8cvt_emitunichar(&s,pending_char);
				}
			}
		}
		else if(c<0xe0) { // 1st byte of a 2-byte sequence
			pending_char = c&0x1f;
			bytes_expected=1;
		}
		else if(c<0xf0) { // 1st byte of a 3-byte sequence
			pending_char = c&0x0f;
			bytes_expected=2;
		}
		else if(c<0xf8) { // 1st byte of a 4-byte sequence
			pending_char = c&0x07;
			bytes_expected=3;
		}
	}
	dst[s.dp] = '\0';
}

// Returns 0 if running on a big-endian system, 1 for little-endian.
IW_IMPL(int) iw_get_host_endianness(void)
{
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union en_union {
		iw_byte c[4];
		int ii;
	} volatile en;

	// Test the host's endianness.
	en.c[0]=0;
	en.ii = 1;
	if(en.c[0]!=0) {
		return 1;
	}
	return 0;
}

IW_IMPL(void) iw_set_ui16le(iw_byte *b, unsigned int n)
{
	b[0] = n&0xff;
	b[1] = (n>>8)&0xff;
}

IW_IMPL(void) iw_set_ui32le(iw_byte *b, unsigned int n)
{
	b[0] = n&0xff;
	b[1] = (n>>8)&0xff;
	b[2] = (n>>16)&0xff;
	b[3] = (n>>24)&0xff;
}

IW_IMPL(void) iw_set_ui16be(iw_byte *b, unsigned int n)
{
	b[0] = (n>>8)&0xff;
	b[1] = n&0xff;
}

IW_IMPL(void) iw_set_ui32be(iw_byte *b, unsigned int n)
{
	b[0] = (n>>24)&0xff;
	b[1] = (n>>16)&0xff;
	b[2] = (n>>8)&0xff;
	b[3] = n&0xff;
}

IW_IMPL(unsigned int) iw_get_ui16le(const iw_byte *b)
{
	return b[0] | (b[1]<<8);
}

IW_IMPL(unsigned int) iw_get_ui32le(const iw_byte *b)
{
	return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}

IW_IMPL(int) iw_get_i32le(const iw_byte *b)
{
	return (iw_int32)(iw_uint32)(b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24));
}

IW_IMPL(unsigned int) iw_get_ui16be(const iw_byte *b)
{
	return (b[0]<<8) | b[1];
}

IW_IMPL(unsigned int) iw_get_ui32be(const iw_byte *b)
{
	return (b[0]<<24) | (b[1]<<16) | (b[2]<<8) | b[3];
}

// Accepts a flag indicating the endianness.
IW_IMPL(unsigned int) iw_get_ui16_e(const iw_byte *b, int is_le)
{
	if(is_le)
		return iw_get_ui16le(b);
	return iw_get_ui16be(b);
}

IW_IMPL(unsigned int) iw_get_ui32_e(const iw_byte *b, int is_le)
{
	if(is_le)
		return iw_get_ui32le(b);
	return iw_get_ui32be(b);
}

IW_IMPL(int) iw_max_color_to_bitdepth(unsigned int mc)
{
	unsigned int bd;

	for(bd=1;bd<=15;bd++) {
		if(mc < (1U<<bd)) return bd;
	}
	return 16;
}

IW_IMPL(int) iw_detect_fmt_from_filename(const char *fn)
{
	char *s;
	s=strrchr(fn,'.');
	if(!s) return IW_FORMAT_UNKNOWN;
	s++;

	if(!iw_stricmp(s,"png")) return IW_FORMAT_PNG;
	if(!iw_stricmp(s,"jpg")) return IW_FORMAT_JPEG;
	if(!iw_stricmp(s,"jpeg")) return IW_FORMAT_JPEG;
	if(!iw_stricmp(s,"bmp")) return IW_FORMAT_BMP;
	if(!iw_stricmp(s,"tif")) return IW_FORMAT_TIFF;
	if(!iw_stricmp(s,"tiff")) return IW_FORMAT_TIFF;
	if(!iw_stricmp(s,"miff")) return IW_FORMAT_MIFF;
	if(!iw_stricmp(s,"webp")) return IW_FORMAT_WEBP;
	if(!iw_stricmp(s,"gif")) return IW_FORMAT_GIF;
	return IW_FORMAT_UNKNOWN;
}

IW_IMPL(const char*) iw_get_fmt_name(int fmt)
{
	static const char *n;
	n=NULL;
	switch(fmt) {
	case IW_FORMAT_PNG:  n="PNG";  break;
	case IW_FORMAT_JPEG: n="JPEG"; break;
	case IW_FORMAT_BMP:  n="BMP";  break;
	case IW_FORMAT_TIFF: n="TIFF"; break;
	case IW_FORMAT_MIFF: n="MIFF"; break;
	case IW_FORMAT_WEBP: n="WebP"; break;
	case IW_FORMAT_GIF:  n="GIF";  break;
	}
	return n;
}

IW_IMPL(int) iw_detect_fmt_of_file(const iw_byte *buf, size_t n)
{
	int fmt = IW_FORMAT_UNKNOWN;

	if(n<2) return fmt;

	if(buf[0]==0x89 && buf[1]==0x50) {
		fmt=IW_FORMAT_PNG;
	}
	else if(n>=3 && buf[0]=='G' && buf[1]=='I' && buf[2]=='F') {
		fmt=IW_FORMAT_GIF;
	}
	else if(buf[0]==0xff && buf[1]==0xd8) {
		fmt=IW_FORMAT_JPEG;
	}
	else if(buf[0]==0x42 && buf[1]==0x4d) {
		fmt=IW_FORMAT_BMP;
	}
	else if((buf[0]==0x49 || buf[0]==0x4d) && buf[1]==buf[0]) {
		fmt=IW_FORMAT_TIFF;
	}
	else if(buf[0]==0x69 && buf[1]==0x64) {
		fmt=IW_FORMAT_MIFF;
	}
	else if(n>=12 && buf[0]==0x52 && buf[1]==0x49 && buf[2]==0x46 && buf[3]==0x46 &&
	   buf[8]==0x57 && buf[9]==0x45 && buf[10]==0x42 && buf[11]==0x50)
	{
		fmt=IW_FORMAT_WEBP;
	}

	return fmt;
}

IW_IMPL(unsigned int) iw_get_profile_by_fmt(int fmt)
{
	unsigned int p;

	switch(fmt) {

	case IW_FORMAT_PNG:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_PALETTETRNS |
		    IW_PROFILE_GRAY1 | IW_PROFILE_GRAY2 | IW_PROFILE_GRAY4 | IW_PROFILE_16BPS |
		    IW_PROFILE_BINARYTRNS | IW_PROFILE_PAL1 | IW_PROFILE_PAL2 | IW_PROFILE_PAL4 |
		    IW_PROFILE_PAL8 | IW_PROFILE_REDUCEDBITDEPTHS | IW_PROFILE_PNG_BKGD;
		break;

	case IW_FORMAT_BMP:
		p = IW_PROFILE_ALWAYSSRGB | IW_PROFILE_PAL1 | IW_PROFILE_PAL4 | IW_PROFILE_PAL8 |
			IW_PROFILE_REDUCEDBITDEPTHS;
		break;

	case IW_FORMAT_JPEG:
		p = IW_PROFILE_GRAYSCALE | IW_PROFILE_ALWAYSSRGB;
		break;

	case IW_FORMAT_TIFF:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_GRAY1 |
		    IW_PROFILE_GRAY4 | IW_PROFILE_16BPS | IW_PROFILE_PAL4 | IW_PROFILE_PAL8;
		break;

	case IW_FORMAT_MIFF:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_ALWAYSLINEAR |
		    IW_PROFILE_HDRI | IW_PROFILE_RGB16_BKGD;
		break;

	case IW_FORMAT_WEBP:
		p = IW_PROFILE_ALWAYSSRGB;
#if IW_WEBP_SUPPORT_TRANSPARENCY
		p |= IW_PROFILE_TRANSPARENCY;
#endif
		break;

	default:
		p = IW_PROFILE_ALWAYSSRGB;
	}

	return p;
}

// The imagew-allfmts.c file is probably a more logical place for the
// iw_is_*_fmt_supported() functions, but putting them there could create
// unnecessary dependencies on third-party libraries.

IW_IMPL(int) iw_is_input_fmt_supported(int fmt)
{
	switch(fmt) {
#if IW_SUPPORT_PNG == 1
	case IW_FORMAT_PNG:
#endif
#if IW_SUPPORT_JPEG == 1
	case IW_FORMAT_JPEG:
#endif
#if IW_SUPPORT_WEBP == 1
	case IW_FORMAT_WEBP:
#endif
	case IW_FORMAT_MIFF:
	case IW_FORMAT_GIF:
	case IW_FORMAT_BMP:
		return 1;
	}
	return 0;
}

IW_IMPL(int) iw_is_output_fmt_supported(int fmt)
{
	switch(fmt) {
#if IW_SUPPORT_PNG == 1
	case IW_FORMAT_PNG:
#endif
#if IW_SUPPORT_JPEG == 1
	case IW_FORMAT_JPEG:
#endif
#if IW_SUPPORT_WEBP == 1
	case IW_FORMAT_WEBP:
#endif
	case IW_FORMAT_BMP:
	case IW_FORMAT_TIFF:
	case IW_FORMAT_MIFF:
		return 1;
	}
	return 0;
}
