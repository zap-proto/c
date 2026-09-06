/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * zap_view.h - reading a ZAP message in place.
 *
 * A ZAP message is a 16-byte head followed by one data run:
 *
 *	 0  magic	"ZAP\0"
 *	 4  version	u16, 1 or 2
 *	 6  flags	u16
 *	 8  root	u32, offset of the root struct
 *	12  size	u32, total bytes
 *
 * Everything after that is addressed by offset. A struct's fixed section
 * holds each field at the byte the schema gave it; a variable tail is a
 * {relative offset u32, length u32} pair; a nested struct is a single
 * relative offset. So a read is an offset and a load - there is no parse
 * pass and nothing is copied.
 *
 * Every reader is total: a field that falls outside the message reads as
 * zero, a pointer that would land in the head or past the end reads as
 * null. A message from the wire cannot make a reader step outside it.
 */

#ifndef ZAP_VIEW_H
#define ZAP_VIEW_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZAP_HEAD 16

/* A message that has passed zap_open: data is exactly the declared size. */
struct zap_msg {
	const uint8_t *data;
	size_t len;
};

/* A struct at some offset inside a message. off 0 means null. */
struct zap_obj {
	const struct zap_msg *msg;
	size_t off;
};

/* A list at some offset, with the element count the wire declared. */
struct zap_list {
	const struct zap_msg *msg;
	size_t off;
	size_t len;
};

/* A run of bytes inside the message. p is NULL when the field is null or
 * the wire pointed outside the message. */
struct zap_bytes {
	const uint8_t *p;
	size_t n;
};

static inline uint16_t zap_u16at(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t zap_u32at(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t zap_u64at(const uint8_t *p)
{
	return (uint64_t)zap_u32at(p) | ((uint64_t)zap_u32at(p + 4) << 32);
}

/* zap_open checks the head and narrows the message to its declared size.
 * Returns 0, or -1 when the magic, the version or the size does not hold. */
static inline int zap_open(struct zap_msg *m, const void *buf, size_t len)
{
	const uint8_t *b = (const uint8_t *)buf;
	uint16_t version;
	uint32_t size;

	m->data = NULL;
	m->len = 0;
	if (len < ZAP_HEAD)
		return -1;
	if (memcmp(b, "ZAP\0", 4) != 0)
		return -1;
	version = zap_u16at(b + 4);
	if (version != 1 && version != 2)
		return -1;
	size = zap_u32at(b + 12);
	if (size < ZAP_HEAD || (size_t)size > len)
		return -1;
	m->data = b;
	m->len = size;
	return 0;
}

static inline uint16_t zap_version(const struct zap_msg *m)
{
	return zap_u16at(m->data + 4);
}

static inline uint16_t zap_flags(const struct zap_msg *m)
{
	return zap_u16at(m->data + 6);
}

static inline struct zap_obj zap_root(const struct zap_msg *m)
{
	struct zap_obj o;

	o.msg = m;
	o.off = zap_u32at(m->data + 8);
	return o;
}

static inline int zap_null(struct zap_obj o) { return !o.msg || o.off == 0; }

static inline uint8_t zap_u8(struct zap_obj o, size_t at)
{
	size_t pos = o.off + at;

	if (!o.msg || pos >= o.msg->len)
		return 0;
	return o.msg->data[pos];
}

static inline uint16_t zap_u16(struct zap_obj o, size_t at)
{
	size_t pos = o.off + at;

	if (!o.msg || pos + 2 > o.msg->len)
		return 0;
	return zap_u16at(o.msg->data + pos);
}

static inline uint32_t zap_u32(struct zap_obj o, size_t at)
{
	size_t pos = o.off + at;

	if (!o.msg || pos + 4 > o.msg->len)
		return 0;
	return zap_u32at(o.msg->data + pos);
}

static inline uint64_t zap_u64(struct zap_obj o, size_t at)
{
	size_t pos = o.off + at;

	if (!o.msg || pos + 8 > o.msg->len)
		return 0;
	return zap_u64at(o.msg->data + pos);
}

static inline int zap_bool(struct zap_obj o, size_t at)
{
	return zap_u8(o, at) != 0;
}

static inline int8_t zap_i8(struct zap_obj o, size_t at)
{
	return (int8_t)zap_u8(o, at);
}

static inline int16_t zap_i16(struct zap_obj o, size_t at)
{
	return (int16_t)zap_u16(o, at);
}

static inline int32_t zap_i32(struct zap_obj o, size_t at)
{
	return (int32_t)zap_u32(o, at);
}

static inline int64_t zap_i64(struct zap_obj o, size_t at)
{
	return (int64_t)zap_u64(o, at);
}

static inline float zap_f32(struct zap_obj o, size_t at)
{
	uint32_t bits = zap_u32(o, at);
	float f;

	memcpy(&f, &bits, sizeof(f));
	return f;
}

static inline double zap_f64(struct zap_obj o, size_t at)
{
	uint64_t bits = zap_u64(o, at);
	double d;

	memcpy(&d, &bits, sizeof(d));
	return d;
}

/* zap_fixed returns n bytes held inline at at - an id, a key, a signature.
 * The run aliases the message and must not be written through. */
static inline struct zap_bytes zap_fixed(struct zap_obj o, size_t at, size_t n)
{
	struct zap_bytes b = {NULL, 0};
	size_t pos = o.off + at;

	if (!o.msg || n == 0 || pos + n > o.msg->len)
		return b;
	b.p = o.msg->data + pos;
	b.n = n;
	return b;
}

/* zap_bytes_at follows a variable tail. The relative offset is an UNSIGNED
 * forward step: a high-bit pattern becomes a large positive step and is
 * refused by the bound below, which is what stops a crafted offset from
 * aliasing back into the fixed section or the head. */
static inline struct zap_bytes zap_bytes_at(struct zap_obj o, size_t at)
{
	struct zap_bytes b = {NULL, 0};
	size_t pos = o.off + at, abs;
	uint32_t rel, n;

	if (!o.msg || pos + 4 > o.msg->len)
		return b;
	rel = zap_u32at(o.msg->data + pos);
	if (rel == 0)
		return b;
	if (pos + 8 > o.msg->len)
		return b;
	n = zap_u32at(o.msg->data + pos + 4);
	abs = pos + rel;
	if (abs < ZAP_HEAD || abs + n > o.msg->len)
		return b;
	b.p = o.msg->data + abs;
	b.n = n;
	return b;
}

/* zap_obj_at follows a nested struct. The relative offset is SIGNED here: a
 * builder may finish a nested struct before its parent, leaving it earlier
 * in the run. Any target inside the head is refused - the head carries the
 * magic, the version, the root and the size, never a payload. */
static inline struct zap_obj zap_obj_at(struct zap_obj o, size_t at)
{
	struct zap_obj n;
	size_t pos = o.off + at;
	int32_t rel;
	long abs;

	n.msg = NULL;
	n.off = 0;
	if (!o.msg || pos + 4 > o.msg->len)
		return n;
	rel = (int32_t)zap_u32at(o.msg->data + pos);
	if (rel == 0)
		return n;
	abs = (long)pos + rel;
	if (abs < ZAP_HEAD || (size_t)abs >= o.msg->len)
		return n;
	n.msg = o.msg;
	n.off = (size_t)abs;
	return n;
}

/* zap_list_at follows a list. The declared length is bounded by the message
 * size, so a length of 0xFFFFFFFF cannot make a caller's loop run four
 * billion times over readers that all return zero. */
static inline struct zap_list zap_list_at(struct zap_obj o, size_t at)
{
	struct zap_list l;
	size_t pos = o.off + at;
	int32_t rel;
	uint32_t n;
	long abs;

	l.msg = NULL;
	l.off = 0;
	l.len = 0;
	if (!o.msg || pos + 8 > o.msg->len)
		return l;
	rel = (int32_t)zap_u32at(o.msg->data + pos);
	if (rel == 0)
		return l;
	n = zap_u32at(o.msg->data + pos + 4);
	if ((size_t)n > o.msg->len)
		return l;
	abs = (long)pos + rel;
	if (abs < ZAP_HEAD || (size_t)abs >= o.msg->len)
		return l;
	l.msg = o.msg;
	l.off = (size_t)abs;
	l.len = n;
	return l;
}

static inline size_t zap_len(struct zap_list l) { return l.len; }

static inline uint8_t zap_list_u8(struct zap_list l, size_t i)
{
	size_t pos = l.off + i;

	if (!l.msg || i >= l.len || pos >= l.msg->len)
		return 0;
	return l.msg->data[pos];
}

static inline uint32_t zap_list_u32(struct zap_list l, size_t i)
{
	size_t pos = l.off + i * 4;

	if (!l.msg || i >= l.len || pos + 4 > l.msg->len)
		return 0;
	return zap_u32at(l.msg->data + pos);
}

static inline uint64_t zap_list_u64(struct zap_list l, size_t i)
{
	size_t pos = l.off + i * 8;

	if (!l.msg || i >= l.len || pos + 8 > l.msg->len)
		return 0;
	return zap_u64at(l.msg->data + pos);
}

/* zap_list_obj returns the i-th element of a list of fixed-size structs. */
static inline struct zap_obj zap_list_obj(struct zap_list l, size_t i,
					  size_t size)
{
	struct zap_obj o;

	o.msg = NULL;
	o.off = 0;
	if (!l.msg || i >= l.len)
		return o;
	o.msg = l.msg;
	o.off = l.off + i * size;
	return o;
}

/* zap_list_at_var returns the i-th element of a list whose elements each
 * carry their own length: a u32 count followed by that many bytes. */
static inline struct zap_bytes zap_list_bytes(struct zap_list l, size_t i)
{
	struct zap_bytes b = {NULL, 0};
	size_t p = l.off, k;

	if (!l.msg || i >= l.len)
		return b;
	for (k = 0; k < i; k++) {
		if (p + 4 > l.msg->len)
			return b;
		p += 4 + (size_t)zap_u32at(l.msg->data + p);
	}
	if (p + 4 > l.msg->len)
		return b;
	b.n = zap_u32at(l.msg->data + p);
	if (p + 4 + b.n > l.msg->len) {
		b.n = 0;
		return b;
	}
	b.p = l.msg->data + p + 4;
	return b;
}

#ifdef __cplusplus
}
#endif

#endif
