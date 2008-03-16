// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file GaleraUtils main header file
 *
 * $ Id: $
 */

#ifndef _galerautils_h_
#define _galerautils_h_

/* "Shamelessly stolen" (tm) goods from Linux kernel */
/*
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */

#define GU_MAX(x,y) ({ \
        typeof(x) _x = (x);  \
        typeof(y) _y = (y);  \
        (void) (&_x == &_y); \
        _x > _y ? _x : _y; })

#define GU_MIN(x,y) ({ \
        typeof(x) _x = (x);  \
        typeof(y) _y = (y);  \
        (void) (&_x == &_y); \
        _x < _y ? _x : _y; })

#define gu_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#include "gu_log.h"
#include "gu_assert.h"
#include "gu_mem.h"
#include "gu_mutex.h"
#include "gu_conf.h"
#include "gu_dbug.h"
#include "gu_byteswap.h"

#endif /* _galerautils_h_ */
