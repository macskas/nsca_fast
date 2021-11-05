//
// Created by macskas on 12/9/20.
//

#ifndef NSCA_COMMON_H
#define NSCA_COMMON_H

#define AUTHOR "macskas"
#include <cstring>
extern "C" {
    // needed for the WORKERS_ENABLED
#include <sys/socket.h>
#include <event2/listener.h>
};

#if (__GNUC__ && __GNUC_MINOR__ && __GNUC_PATCHLEVEL__)
#define GCC_VERSION (__GNUC__ * 10000 \
    + __GNUC_MINOR__ * 100 \
    + __GNUC_PATCHLEVEL__)
#elif (__cplusplus)
#define GCC_VERSION __cplusplus
#else
#define GCC_VERSION 0
#endif

#ifdef __x86_64__
#define VERSION_ARCH "x86_64"
#else
#define VERSION_ARCH "i386"
#endif

#ifdef __DATE__
#define VERSION_DATE __DATE__
#else
#define VERSION_DATE "??? ??? ??"
#endif

#ifdef __TIMESTAMP__
#define VERSION_DATE_LONG __TIMESTAMP__
#else
#define VERSION_DATE_LONG "??? ??? ?? ??:??:?? ????"
#endif

#define VERSION_STRING "nsca/" VERSION_ARCH

#define GZIP_CHUNK_SIZE     16384
#define DBUFFER_4K_SIZE		4096
#define DBUFFER_32K_SIZE	32768
#define DMEMZERO( _aPtr, _aSize ) memset( ( _aPtr ), 0, ( _aSize ) )
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define DECRYPTION_MODE_SHARED_CRYPT_INSTANCE 1

#if defined(LEV_OPT_REUSEABLE_PORT) && defined(SO_REUSEPORT)
    #define WORKERS_ENABLED 1
#endif

#ifndef LEV_OPT_REUSEABLE_PORT
    #define LEV_OPT_REUSEABLE_PORT 0
#endif

#endif //NSCA_COMMON_H
