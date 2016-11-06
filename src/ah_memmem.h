#pragma once

#if defined __cplusplus
extern "C" {
#endif

#if defined __GNUC__	// use the memmem provided by gnuc
#define ah_memmem memmem
#else	// if not gnuc

/* Return the first occurrence of NEEDLE in HAYSTACK. Return HAYSTACK
 if NEEDLE_LEN is 0, otherwise NULL if NEEDLE is not found in
 HAYSTACK. */
void *
	ah_memmem (const void *haystack_start, size_t haystack_len,
	const void *needle_start, size_t needle_len);

#endif

#if defined __cplusplus
}
#endif
