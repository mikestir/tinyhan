/*
 * Copyright 2013-2014 Mike Stirling
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of the Tiny Home Area Network stack.
 *
 * http://www.tinyhan.co.uk/
 *
 * common.h
 *
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>

#include "ansi.h"

typedef unsigned int boolean_t;

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

#define TRACE(a,...)	fprintf(stderr, ATTR_RESET FG_GREEN __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__)
#define INFO(a,...)		fprintf(stderr, ATTR_RESET FG_YELLOW __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__)
#define ERROR(a,...)	fprintf(stderr, ATTR_RESET FG_RED __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__)
#define FUNCTION_TRACE	TRACE("%s\n", __FUNCTION__)
#else
#define DUMP(buf, size)
#define TRACE(a,...)
#define INFO(a,...)
#define ERROR(a,...)
#define FUNCTION_TRACE
#endif

#define PACKED			__attribute__((packed))
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(a[0]))

#endif /* COMMON_H_ */
