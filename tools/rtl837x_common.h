/* Host-side shim for compiling the SDCC-targeted crypto sources (crypto/)
 * into the httpd simulator. Maps SDCC storage class keywords to empty and
 * provides the few helpers the crypto code uses from the firmware. */
#ifndef _RTL837X_COMMON_SHIM_H_
#define _RTL837X_COMMON_SHIM_H_

#define __xdata
#define __code
#define __reentrant
#define __banked
#define codeseg(x)

#include <stdint.h>
#include <string.h>

void print_string(const char *s);
void print_byte(unsigned char b);
void memcpyc(void *dst, const void *src, unsigned int len);

#endif
