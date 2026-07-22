#ifndef PULP_UDMA_FILTER_H
#define PULP_UDMA_FILTER_H

#include <stdint.h>
#include <zephyr/kernel.h>

static inline void pulp_mult_q7(const q7_t *src_a, const q7_t *src_b, q7_t *dst,
				uint32_t block_size);
static inline void pulp_mult_q15(const q15_t *src_a, const q15_t *src_b, q15_t *dst,
				 uint32_t block_size);
static inline void pulp_mult_q31(const q31_t *src_a, const q31_t *src_b, q31_t *dst,
				 uint32_t block_size);

static inline void pulp_add_q7(const q7_t *src_a, const q7_t *src_b, q7_t *dst, uint32_t block_size);

static inline void pulp_add_q15(const q15_t *src_a, const q15_t *src_b, q15_t *dst,
				uint32_t block_size);
static inline void pulp_add_q31(const q31_t *src_a, const q31_t *src_b, q31_t *dst,
				uint32_t block_size);

static inline void pulp_sub_q7(const q7_t *src_a, const q7_t *src_b, q7_t *dst, uint32_t block_size);
static inline void pulp_sub_q15(const q15_t *src_a, const q15_t *src_b, q15_t *dst,
				uint32_t block_size);
static inline void pulp_sub_q31(const q31_t *src_a, const q31_t *src_b, q31_t *dst,
				uint32_t block_size);

static inline void pulp_scale_q7(const q7_t *src, q7_t scale_fract, int8_t shift, q7_t *dst,
				 uint32_t block_size);
static inline void pulp_scale_q15(const q15_t *src, q15_t scale_fract, int8_t shift, q15_t *dst,
				  uint32_t block_size);
static inline void pulp_scale_q31(const q31_t *src, q31_t scale_fract, int8_t shift, q31_t *dst,
				  uint32_t block_size);

static inline void pulp_abs_q7(const q7_t *src, q7_t *dst, uint32_t block_size);
static inline void pulp_abs_q15(const q15_t *src, q15_t *dst, uint32_t block_size);
static inline void pulp_abs_q31(const q31_t *src, q31_t *dst, uint32_t block_size);

static inline void pulp_negate_q7(const q7_t *src, q7_t *dst, uint32_t block_size);
static inline void pulp_negate_q15(const q15_t *src, q15_t *dst, uint32_t block_size);
static inline void pulp_negate_q31(const q31_t *src, q31_t *dst, uint32_t block_size);

static inline void pulp_dot_prod_q7(const q7_t *src_a, const q7_t *src_b, uint32_t block_size,
				    q31_t *dst);
static inline void pulp_dot_prod_q15(const q15_t *src_a, const q15_t *src_b, uint32_t block_size,
				     q63_t *dst);
static inline void pulp_dot_prod_q31(const q31_t *src_a, const q31_t *src_b, uint32_t block_size,
				     q63_t *dst);

static inline void pulp_shift_q7(const q7_t *src, int8_t shift_bits, q7_t *dst, uint32_t block_size);
static inline void pulp_shift_q15(const q15_t *src, int8_t shift_bits, q15_t *dst,
				  uint32_t block_size);
static inline void pulp_shift_q31(const q31_t *src, int8_t shift_bits, q31_t *dst,
				  uint32_t block_size);

static inline void pulp_offset_q7(const q7_t *src, q7_t offset, q7_t *dst, uint32_t block_size);
static inline void pulp_offset_q15(const q15_t *src, q15_t offset, q15_t *dst, uint32_t block_size);
static inline void pulp_offset_q31(const q31_t *src, q31_t offset, q31_t *dst, uint32_t block_size);

static inline void pulp_clip_q7(const q7_t *src, q7_t *dst, q7_t low, q7_t high,
				uint32_t num_samples);
static inline void pulp_clip_q15(const q15_t *src, q15_t *dst, q15_t low, q15_t high,
				 uint32_t num_samples);
static inline void pulp_clip_q31(const q31_t *src, q31_t *dst, q31_t low, q31_t high,
				 uint32_t num_samples);

static inline void pulp_and_u8(const uint8_t *src_a, const uint8_t *src_b, uint8_t *dst,
			       uint32_t block_size);
static inline void pulp_and_u16(const uint16_t *src_a, const uint16_t *src_b, uint16_t *dst,
				uint32_t block_size);
static inline void pulp_and_u32(const uint32_t *src_a, const uint32_t *src_b, uint32_t *dst,
				uint32_t block_size);

static inline void pulp_or_u8(const uint8_t *src_a, const uint8_t *src_b, uint8_t *dst,
			      uint32_t block_size)
static inline void pulp_or_u16(const uint16_t *src_a, const uint16_t *src_b, uint16_t *dst,
			       uint32_t block_size);
static inline void pulp_or_u32(const uint32_t *src_a, const uint32_t *src_b, uint32_t *dst,
			       uint32_t block_size);

static inline void pulp_xor_u8(const uint8_t *src_a, const uint8_t *src_b, uint8_t *dst,
			       uint32_t block_size);
static inline void pulp_xor_u16(const uint16_t *src_a, const uint16_t *src_b, uint16_t *dst,
				uint32_t block_size);
static inline void pulp_xor_u32(const uint32_t *src_a, const uint32_t *src_b, uint32_t *dst,
				uint32_t block_size);

static inline void pulp_not_u8(const uint8_t *src, uint8_t *dst, uint32_t block_size);
static inline void pulp_not_u16(const uint16_t *src, uint16_t *dst, uint32_t block_size);
static inline void pulp_not_u32(const uint32_t *src, uint32_t *dst, uint32_t block_size);

#endif