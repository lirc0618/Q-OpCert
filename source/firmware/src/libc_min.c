#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int value, size_t count)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < count; ++i) d[i] = (unsigned char)value;
    return dst;
}

/* ARM EABI helpers emitted by Clang for aligned structure initialization. */
void __aeabi_memclr(void *dst, size_t count)
{
    (void)memset(dst, 0, count);
}

void __aeabi_memclr4(void *dst, size_t count)
{
    (void)memset(dst, 0, count);
}

void __aeabi_memclr8(void *dst, size_t count)
{
    (void)memset(dst, 0, count);
}
