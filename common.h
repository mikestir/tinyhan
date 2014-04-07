/*
 * common.h
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>

#ifndef TRUE
#define TRUE ~0
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifdef DEBUG
#define DUMP(buf, size) \
{\
	int n;\
	for (n = 0; n < size; n++) {\
		fprintf(stderr, "%02X ",(unsigned char)buf[n]);\
		if ((n & 15) == 15)\
			fprintf(stderr, "\n");\
	}\
	fprintf(stderr, "\n");\
}

#define TRACE(a,...)	fprintf(stderr, a, ##__VA_ARGS__)
#define INFO(a,...)		fprintf(stderr, a, ##__VA_ARGS__)
#define ERROR(a,...)	fprintf(stderr, a, ##__VA_ARGS__)
#define FUNCTION_TRACE	TRACE("%s\n", __FUNCTION__)
#else
#define DUMP(buf, size)
#define TRACE(a,...)
#define INFO(a,...)
#define ERROR(a,...)
#define FUNCTION_TRACE
#endif

#endif /* COMMON_H_ */
