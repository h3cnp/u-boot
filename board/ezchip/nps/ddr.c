/*
* Copyright (c) 2016, Mellanox Technologies. All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenIB.org BSD license below:
*
*     Redistribution and use in source and binary forms, with or
*     without modification, are permitted provided that the following
*     conditions are met:
*
*      - Redistributions of source code must retain the above
*        copyright notice, this list of conditions and the following
*        disclaimer.
*
*      - Redistributions in binary form must reproduce the above
*        copyright notice, this list of conditions and the following
*        disclaimer in the documentation and/or other materials
*        provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <common.h>
#include <asm/io.h>
#include <linux/errno.h>
#include "common.h"
#include "ddr.h"
#include "nps.h"
#include "chip.h"
#include "bist.h"

#ifdef CONFIG_TARGET_NPS_SIM
int	g_EZsim_mode = 1;
#else
int	g_EZsim_mode = 0;
#endif

#define VREF_PER_RANK

u32 g_ddr_dump_verbose=0;
u32 g_ddr_htol_debug_verbose=1;

u32 phy_init_fail_block = 0;

static int skip_mc_mask = 0;
static int do_htol_wa = true;
bool g_int_lb_mode = false;
static void configure_relax_mc(void);
static void set_ddr_double_refresh(void);
static void configure_emem_mi(void);
static void ddr_phy_reset_internal_read_fifos(u32 port);

/* command pup_fail_dump sets flag to print all pub registers in case of failure */
bool g_pup_fail_dump = false;
/* pause between steps */
bool g_ddr_pause = false;
u32 g_ddr_training_pir_val= INVALID_VAL;
void phy_bist_setup(u32 mc_mask);
void phy_bist_run(u32 mc_mask, u32 *results);

static int phy_ck_setup(void);
static void get_ddr_parameters(void);

#define DDR_TRAINING_CLI_MODE \
	(g_ddr_training_pir_val != INVALID_VAL)

#define emem_mc_indirect_reg_write_apb_data0_data1(block, address,\
						data_0, data_1) \
			emem_mc_write_indirect_reg(block, address, 2,\
						data_0, data_1, 0x101)

#define emem_mc_indirect_reg_write_mc(block, address, data_0, data_1)	\
		do {							\
			emem_mc_write_indirect_reg(block, address, 2,	\
						data_0, data_1, 0x1);	\
			emem_mc_write_indirect_reg(block, address, 2,	\
						data_0, data_1, 0x11);	\
		} while (0)

#define SKIP_IFC(ifc)	(GET_BITS(skip_mc_mask, ifc * 2, 2) == 0x3)

#define SKIP_MC(_mc)	(GET_BIT(skip_mc_mask, _mc))

static const int emem_mi_block_id[EMEM_MI_NUM_OF_BLOCKS] = {
	0x610, 0x611, 0x612, 0x613, 0x614, 0x615,
	0x710, 0x711, 0x712, 0x713, 0x714, 0x715
};

const int emem_mc_block_id[EMEM_MC_NUM_OF_BLOCKS] = {
	0x618, 0x619, 0x61a, 0x61b, 0x61c, 0x61d,
	0x718, 0x719, 0x71a, 0x71b, 0x71c, 0x71d
	};

struct pub_regs_synop pub_regs_synop_val[] = {
	/* please keep this register first*/
	{PUB_RANKIDR_REG_ADDR, 0x0},

	{PUB_PGCR2_REG_ADDR, ((0xBAD << 16) | PUB_PGCR2_REG_ADDR)},
	{PUB_PGCR3_REG_ADDR, ((0xBAD << 16) | PUB_PGCR3_REG_ADDR)},
	{PUB_PGCR4_REG_ADDR, ((0xBAD << 16) | PUB_PGCR4_REG_ADDR)},
	{PUB_PGCR5_REG_ADDR, ((0xBAD << 16) | PUB_PGCR5_REG_ADDR)},
	{PUB_PGSR1_REG_ADDR, ((0xBAD << 16) | PUB_PGSR1_REG_ADDR)},
	{PUB_PTR0_REG_ADDR, ((0xBAD << 16) | PUB_PTR0_REG_ADDR)},
	{PUB_PTR1_REG_ADDR, ((0xBAD << 16) | PUB_PTR1_REG_ADDR)},
	{PUB_PTR2_REG_ADDR, ((0xBAD << 16) | PUB_PTR2_REG_ADDR)},
	{PUB_PTR3_REG_ADDR, ((0xBAD << 16) | PUB_PTR3_REG_ADDR)},
	{PUB_PTR4_REG_ADDR, ((0xBAD << 16) | PUB_PTR4_REG_ADDR)},
	{PUB_DXCCR_REG_ADDR, ((0xBAD << 16) | PUB_DXCCR_REG_ADDR)},
	{PUB_ODTCR_REG_ADDR, ((0xBAD << 16) | PUB_ODTCR_REG_ADDR)},
	{PUB_AACR_REG_ADDR, ((0xBAD << 16) | PUB_AACR_REG_ADDR)},
	{PUB_DTPR6_REG_ADDR, ((5 << 8) | (5 << 0))},
	{PUB_SCHCR0_REG_ADDR, (5 << 8)},
	{PUB_SCHCR1_REG_ADDR, 0 },

	{PUB_PGCR6_REG_ADDR, ((1 << 12) | (1 << 13) | (1 << 16))},

	{PUB_DX0LCDLR1_REG_ADDR, 0x0},
	{PUB_DX1LCDLR1_REG_ADDR, 0x0},
	{PUB_DX2LCDLR1_REG_ADDR, 0x0},
	{PUB_DX3LCDLR1_REG_ADDR, 0x0},

	{PUB_DX0LCDLR3_REG_ADDR, 0x0},
	{PUB_DX1LCDLR3_REG_ADDR, 0x0},
	{PUB_DX2LCDLR3_REG_ADDR, 0x0},
	{PUB_DX3LCDLR3_REG_ADDR, 0x0},

	{PUB_DX0LCDLR4_REG_ADDR, 0x0},
	{PUB_DX1LCDLR4_REG_ADDR, 0x0},
	{PUB_DX2LCDLR4_REG_ADDR, 0x0},
	{PUB_DX3LCDLR4_REG_ADDR, 0x0},

	{PUB_DX0MDLR0_REG_ADDR, 0x0},
	{PUB_DX1MDLR0_REG_ADDR, 0x0},
	{PUB_DX2MDLR0_REG_ADDR, 0x0},
	{PUB_DX3MDLR0_REG_ADDR, 0x0},

	{PUB_DX0BDLR0_REG_ADDR, 0x0},
	{PUB_DX1BDLR0_REG_ADDR, 0x0},
	{PUB_DX2BDLR0_REG_ADDR, 0x0},
	{PUB_DX3BDLR0_REG_ADDR, 0x0},

	{PUB_DX0BDLR1_REG_ADDR, 0x0},
	{PUB_DX1BDLR1_REG_ADDR, 0x0},
	{PUB_DX2BDLR1_REG_ADDR, 0x0},
	{PUB_DX3BDLR1_REG_ADDR, 0x0},

	{PUB_DX0BDLR3_REG_ADDR, 0x0},
	{PUB_DX1BDLR3_REG_ADDR, 0x0},
	{PUB_DX2BDLR3_REG_ADDR, 0x0},
	{PUB_DX3BDLR3_REG_ADDR, 0x0},

	{PUB_DX0BDLR4_REG_ADDR, 0x0},
	{PUB_DX1BDLR4_REG_ADDR, 0x0},
	{PUB_DX2BDLR4_REG_ADDR, 0x0},
	{PUB_DX3BDLR4_REG_ADDR, 0x0},

	{PUB_BISTRR_REG_ADDR, PUB_BISTRR_REG_HW_DEF_VAL},
	{PUB_BISTAR1_REG_ADDR, (0xF << 16)},
	{PUB_BISTWCR_REG_ADDR, 0x20},
	{PUB_DX0GCR4_REG_ADDR, ((0x3 << 26) | (0x1 << 25) | (0xF << 2))},
	{PUB_DX1GCR4_REG_ADDR, ((0x3 << 26) | (0x1 << 25) | (0xF << 2))},
	{PUB_DX2GCR4_REG_ADDR, ((0x3 << 26) | (0x1 << 25) | (0xF << 2))},
	{PUB_DX3GCR4_REG_ADDR, ((0x3 << 26) | (0x1 << 25) | (0xF << 2))},

	{PUB_DX0GCR5_REG_ADDR, ((0x9 << 24) | (0x9 << 16) | (0x9 << 8) | (0x9 << 0))},
	{PUB_DX1GCR5_REG_ADDR, ((0x9 << 24) | (0x9 << 16) | (0x9 << 8) | (0x9 << 0))},
	{PUB_DX2GCR5_REG_ADDR, ((0x9 << 24) | (0x9 << 16) | (0x9 << 8) | (0x9 << 0))},
	{PUB_DX3GCR5_REG_ADDR, ((0x9 << 24) | (0x9 << 16) | (0x9 << 8) | (0x9 << 0))},

	{PUB_DX0GCR6_REG_ADDR, 0x09090909},
	{PUB_DX1GCR6_REG_ADDR, 0x09090909},
	{PUB_DX2GCR6_REG_ADDR, 0x09090909},
	{PUB_DX3GCR6_REG_ADDR, 0x09090909},

	{PUB_PIR_REG_ADDR, 0x0},
	{PUB_ACBDLR0_REG_ADDR, 0x0},
	{PUB_DTCR0_REG_ADDR, 0x8000B187},
	{PUB_BISTUDPR_REG_ADDR, 0xFFFF0000},
	{PUB_BISTAR2_REG_ADDR, 0xF0000FFF},
	{PUB_BISTAR4_REG_ADDR, 0x0003FFFF},
	{PUB_BISTLSR_REG_ADDR, 0x1234ABCD},

	{PUB_ACMDLR0_REG_ADDR, 0x5},
	{PUB_ZQ1PR_REG_ADDR, 0x0007BB7B},
	{PUB_RIDR_REG_ADDR, 0x00250231},
	{PUB_GPR0_REG_ADDR, 0x0},
	{PUB_GPR1_REG_ADDR, 0x0},
	{PUB_PGSR0_REG_ADDR, 0x0},
	{PUB_PGCR0_REG_ADDR, 0x07D81E00},
	{PUB_PGCR1_REG_ADDR, 0x02004620},
	{PUB_PGCR7_REG_ADDR, 0x40000},
	{PUB_VTCR0_REG_ADDR, 0x70032019},
	{PUB_VTCR1_REG_ADDR, 0x0FC00072},
	{PUB_BISTGSR_REG_ADDR, 0x0},
	{PUB_PLLCR_REG_ADDR, 0x38000},
	{PUB_DSGCR_REG_ADDR, 0x64641B},
	{PUB_DCR_REG_ADDR, 0x40B},
	{PUB_ZQCR_REG_ADDR, 0x4058D00},
	{PUB_ZQ0PR_REG_ADDR, 0x7BB7B},
	{PUB_ZQ1PR_REG_ADDR, 0x7BB7B},
	{PUB_DX0GCR4_REG_ADDR, 0x0E00003C},
	{PUB_DTPR5_REG_ADDR, 0x320E08},
	{PUB_DTPR4_REG_ADDR, 0x176081A},
	{PUB_DTPR3_REG_ADDR, 0x2000101},
	{PUB_DTPR2_REG_ADDR, 0x60200},
	{PUB_DTPR1_REG_ADDR, 0x28260402},
	{PUB_DTPR0_REG_ADDR, 0x7240E08},

	{PUB_BISTMSKR1_REG_ADDR, 0},
	{PUB_DTEDR0_REG_ADDR, 0},
	{PUB_DTEDR1_REG_ADDR, 0},
	{PUB_DTEDR2_REG_ADDR, 0},
	{PUB_VTDR_REG_ADDR, ((0x3F << 24) | (0x3F << 8))},

	{PUB_MR0_REG_ADDR, 0x0},
	{PUB_MR1_REG_ADDR, 0x0},
	{PUB_MR2_REG_ADDR, 0x0},
	{PUB_MR3_REG_ADDR, 0x0},
	{PUB_MR4_REG_ADDR, 0x0},
	{PUB_MR5_REG_ADDR, 0x0},
	{PUB_MR6_REG_ADDR, 0x0}
};

static struct ddr_params ddr_config_0 = {
	DDR4_8BIT, 0x6000, 0xFFFFFF, 1200, false,
	DDR4_2400R, 0xFFFFFFFF, 50, /* clk_ref */
	{ 596, 678, 628, 651, 493, 566, 529, 555, 663, 672, 626, 624,
	  564, 658, 633, 664, 486, 564, 521, 550, 633, 677, 600, 578 },
	{ false, false, false, false, false, false, false, false, false, false,
	  false, false, false, false, false, false, false, false, false, false,
	  false, false, false, false },
	50, /* tcase*/
	false,
	RZQ_DIV_7,
	RZQ_DIV_5,
	RZQ_DIV_7,
	DISABLED,
	RZQ_DIV_3,
	780, /* mV */
	850, /* mV */  /* TODO - WA for HOST_VREF value, original value was 850 */
	{ { 0, 82 }, { 0, 82 }, { 0, 82 }, { 0, 82 },
	  { 0, 82 }, { 0, 82 }, { 0, 82 }, { 0, 82 },
	  { 0, 82 }, { 0, 82 }, { 0, 82 }, { 0, 82 } },
	  DDR_1200_MHz,
	false /* ddr_double_refresh */
};

struct ddr_params  current_ddr_params;
/* table 7.2.10 */
const u32 vref_dq_2[51] = {
		4500, 4565, 4630, 4695, 4760, 4825, 4890, 4955, 5020, 5085,
		5150, 5215, 5280, 5345, 5410, 5475, 5540, 5605, 5670, 5735,
		5800, 5865, 5930, 5995, 6060, 6125, 6190, 6255, 6320, 6385,
		6450, 6515, 6580, 6645, 6710, 6775, 6840, 6905, 6970, 7035,
		7100, 7165, 7230, 7295, 7360, 7425, 7490, 7555, 7620, 7685,
		7750
};
/* table 7.2.9 */
const u32 vref_dq_1[51] = {
		6000, 6065, 6130, 6195, 6260, 6325, 6390, 6455, 6520, 6585,
		6650, 6715, 6780, 6845, 6910, 6975, 7040, 7105, 7170, 7235,
		7300, 7365, 7430, 7495, 7560, 7625, 7690, 7755, 7820, 7885,
		7950, 8015, 8080, 8145, 8210, 8275, 8340, 8405, 8470, 8535,
		8600, 8665, 8730, 8795, 8860, 8925, 8990, 9055, 9120, 9185,
		9250
};

const u32 phy_rd_vref_dq[64] = {
		4407, 4476, 4546, 4616, 4686, 4756, 4825, 4895, 4965, 5035,
		5105, 5174, 5244, 5314, 5384, 5454, 5523, 5593, 5663, 5733,
		5803, 5872, 5942, 6012, 6082, 6152, 6221, 6291, 6361, 6431,
		6501, 6570, 6640, 6710, 6780, 6850, 6919, 6989, 7059, 7129,
		7199, 7268, 7338, 7408, 7478, 7548, 7617, 7687, 7757, 7827,
		7897, 7966, 8036, 8106, 8176, 8246, 8315, 8385, 8455, 8525,
		8595, 8664, 8734, 8804
};

static const u32 clk_ref_100[NUM_OF_DEFAULTS_FREQ] = {
	0x000C0C01, 0x00090C01, 0x00082803, 0x00060C01, 0x0005A683, 0x00060E81, 0x00050C81, 0x00041F03,
	0x00048C01, 0x00040B01, 0x00052A83, 0x00042303, 0x00040C01, 0x00042803, 0x00030B01, 0x00030C01,
};

static const u32 clk_ref_50[NUM_OF_DEFAULTS_FREQ] = {
	0x000C1801, 0x00091801, 0x00079901, 0x00061801, 0x00049501, 0x00061D01, 0x00051901, 0x00043E03,
	0x00049801, 0x00051B81, 0x00049981, 0x00044603, 0x00041801, 0x00045003, 0x00031601, 0x00031801
};

static const u32 cwl_ddr4[NUM_OF_DEFAULTS_FREQ][NUM_OF_DDR4_SPEED_BIN] = {
	       { 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
/*1.875<2.5*/  { 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
/*1.5<1.875*/  { 9, 9, 9, 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9 },
/*1.25<1.5*/   { 9, 9, 9, 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9 },
/*1.07<1.25*/  { 0, 0, 0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 },

/*0.938<1.07*/ { 0, 0, 0, 0,  0,  0,  11, 11, 11, 11, 11, 11, 11, 11, 11, 11 },
/*0.938<1.07*/ { 0, 0, 0, 0,  0,  0,  11, 11, 11, 11, 11, 11, 11, 11, 11, 11 },
/*0.938<1.07*/ { 0, 0, 0, 0,  0,  0,  11, 11, 11, 11, 11, 11, 11, 11, 11, 11 },

/*0.938<1.07*/ { 0, 0, 0, 0,  0,  0,  11, 11, 11, 11, 11, 11, 11, 11, 11, 11 },

/*0.833<0.938*/{ 0, 0, 0, 0,  0,  0,  0,  0,  0,  12, 12, 12, 12, 12, 12, 12 },
/*0.833<0.938*/{ 0, 0, 0, 0,  0,  0,  0,  0,  0,  12, 12, 12, 12, 12, 12, 12 },
/*0.833<0.938*/{ 0, 0, 0, 0,  0,  0,  0,  0,  0,  12, 12, 12, 12, 12, 12, 12 },

/*0.833<0.938*/{ 0, 0, 0, 0,  0,  0,  0,  0,  0,  12, 12, 12, 12, 12, 12, 12 },
/*0.75<0.833*/ { 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  14, 14, 14, 14 },
};

static const u32 cwl_ddr3[6][NUM_OF_DDR3_SPEED_BIN] = {
	{ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
	{ 0, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
	{ 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9, 9, 9 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 10, 10, 10 }
};

static const u32 cl_ddr4[NUM_OF_DEFAULTS_FREQ][NUM_OF_DDR4_SPEED_BIN] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 9, 9, 10, 9, 9, 10, 9, 9, 10, 9, 9, 10, 10, 9, 9, 10, 10},
/*1.25-1.5*/	{ 10, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11, 11, 12, 12},
/*1.071 - 1.25  933MHz*/{ 0, 0, 0, 12, 13, 14, 14, 14, 14, 14, 14, 14, 14, 13, 13, 14, 14},

/*0.937 - 1.071  966MHz*/{ 0, 0, 0, 0, 0, 0, 14, 15, 16, 16, 16, 16, 16, 15, 15, 16, 16},
/*0.937 - 1.071 1000MHz*/{ 0, 0, 0, 0, 0, 0, 14, 15, 16, 16, 16, 16, 16, 15, 15, 16, 16},
/*0.937 - 1.071  1033MHz*/{ 0, 0, 0, 0, 0, 0, 14, 15, 16, 16, 16, 16, 16, 15, 15, 16, 16},

/*0.937 - 1.071 <=1066MHz*/	{ 0, 0, 0, 0, 0, 0, 14, 15, 16, 16, 16, 16, 16, 15, 15, 16, 16},

/*0.833 - 0.937 <=1100MHz*/	{ 0, 0,  0,  0,  0,  0,  0,  0,  0,  15, 16, 17, 18, 16, 17, 18, 18},
/*0.833 - 0.937 <=1133MHz*/	{ 0, 0,  0,  0,  0,  0,  0,  0,  0,  15, 16, 17, 18, 16, 17, 18, 18},
/*0.833 - 0.937 <=1166MHz*/	{ 0, 0,  0,  0,  0,  0,  0,  0,  0,  15, 16, 17, 18, 16, 17, 18, 18},

/*0.833 - 0.937 <=1200MHz*/	{ 0, 0,  0,  0,  0,  0,  0,  0,  0,  15, 16, 17, 18, 16, 17, 18, 18},

/* 1333MHz */	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 18, 19, 20},
/*DDR_1466_MHz*/	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 18, 19, 20},
/*DDR_1600_MHz*/	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 18, 19, 20},
};

static const u32 cl_ddr3[6][NUM_OF_DDR3_SPEED_BIN] = {
{ 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
{ 0, 0, 6, 7, 8, 6, 7, 8, 8, 6, 6, 7, 8, 7, 7, 8, 6, 6, 6, 7, 7 },
{ 0, 0, 0, 0, 0, 7, 8, 9, 10, 7, 8, 9, 10, 8, 9, 10, 8, 7, 8, 9, 9 },
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 10, 11, 10, 11, 11, 9, 9, 9, 10, 11 },
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 12, 13, 11, 10, 11, 12, 13 },
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 12, 13, 14 }
};

static const u32 trcd_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	12500, 15000, 11250, 13125, 15000, 10500, 12000, 13500, 15000, 10000,
	11250, 12500, 13750, 10700, 11770, 12840, 13910, 10285, 11220, 12155,
	13090
};

static const u32 trcd_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	12500, 13750, 15000, 12850, 13920, 15000, 13130, 14060,
	15000, 12500, 13320, 14160, 15000, 12750, 13500, 14250, 15000
};

static const u32 twlo_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 9000, 7500, 7500,
	7500, 7500, 7500, 7500, 7500, 7500, 7500, 7500, 7500, 7500
};

static const u32 twlo_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	9500, 9500, 9500, 9500, 9500, 9500, 9500, 9500,
	9500, 9500, 9500, 9500, 9500, 9500, 9500, 9500, 9500
};

static const u32 txp_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	7500, 7500, 7500, 7500, 7500, 6000, 6000, 6000, 6000, 6000, 6000,
	6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000
};

static const u32 txp_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000,
	6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000
};

static const u32 tcke_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	7500, 7500, 5625, 5625, 5625, 5625, 5625, 5625, 5625, 5000, 5000,
	5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000,
};

static const u32 tcke_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000,
	5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000
};

static const u32 trrd_1kb_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	10000, 10000, 7500, 7500, 7500, 6000, 6000, 6000, 6000, 6000,
	6000, 6000, 6000, 5000, 5000, 5000, 5000, 5000, 5000, 5000, 5000
};

static const u32 trrd_2kb_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	10000, 10000, 10000, 10000, 10000, 7500, 7500, 7500, 7500, 7500, 7500,
	7500, 7500, 6000, 6000, 6000, 6000, 6000, 6000, 6000, 6000
};

static const u32 trrd_1kb_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	6000, 6000, 6000, 5300, 5300, 5300, 5300, 5300,
	5300, 4900, 4900, 4900, 4900, 4900, 4900, 4900, 4900
};

static const u32 trrd_2kb_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	7500, 7500, 7500, 6400, 6400, 6400, 6400, 6400,
	6400, 6400, 6400, 6400, 6400, 6400, 6400, 6400
};

static const u32 tRP_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	12500, 13750, 15000, 12850, 13920, 15000, 13130, 14060,
	15000, 12500, 13320, 14160, 15000, 12750, 13500, 14250, 15000
};

static const u32 tRP_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
		12500, 15000, 11250, 13125, 15000, 10500, 12000, 13500, 15000,
		10000, 11250, 12500, 13750, 10700, 11770, 12840, 13910, 10285,
		11220, 12155, 13090
};

static const u32 tFAW_ddr4_x8[NUM_OF_DDR4_SPEED_BIN] = {
		25000, 25000, 25000, 23000, 23000, 23000, 21000, 21000,
		21000, 21000, 21000, 21000, 21000, 21000, 21000, 21000, 21000
};

static const u32 tFAW_ddr4_x16[NUM_OF_DDR4_SPEED_BIN] = {
		35000, 35000, 35000, 30000, 30000, 30000, 30000, 30000,
		30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000
};

static const u32 tFAW_ddr3_x8[NUM_OF_DDR3_SPEED_BIN] = {
		40000, 40000, 37500, 37500, 37500, 30000, 30000, 30000, 30000,
		30000, 30000, 30000, 30000, 27000, 27000, 27000, 27000, 25000,
		25000, 25000, 25000
};

/* should be used also when density is 8Gbit */
static const u32 tFAW_ddr3_x16[NUM_OF_DDR3_SPEED_BIN] = {
		50000, 50000, 50000, 50000, 50000, 45000, 45000, 45000, 45000,
		40000, 40000, 40000, 40000, 35000, 35000, 35000, 35000, 35000,
		35000, 35000, 35000
};

static const u32 tDQSCK_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
		400, 400, 300, 300, 300, 225, 225, 225, 225, 225, 225,
		225, 225, 195, 195, 195, 195, 180, 180, 180, 180
};

static const u32 tDQSCK_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
		225, 225, 225, 195, 195, 195, 180, 180,
		180, 170, 170, 170, 170, 160, 160, 160, 160
};

static const u32 trc_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
		50000, 52500, 48750, 50625, 52500, 46500, 48000, 49000, 51000,
		45000, 46250, 47500, 48750, 44700, 45770, 46840, 47910, 43285,
		44220, 45155, 46090
};

static const u32 trc_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
		47500, 48500, 50000, 46850, 47920, 49000, 46130, 47060,
		48000, 44500, 45320, 46160, 47000, 44750, 45500, 46250, 47000
};

static const u32 tRAS_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
		37500, 37500, 37500, 37500, 37500, 36000, 36000, 36000, 36000,
		35000, 35000, 35000, 35000, 34000, 34000, 34000, 34000, 33000,
		33000, 33000, 33000
};

static const u32 tRAS_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
		35000, 35000, 35000, 34000, 34000, 34000, 33000, 33000, 33000,
		32000, 32000, 32000, 32000, 32000, 32000, 32000, 32000
};

static const u32 threshold_ddr3[NUM_OF_DDR3_SPEED_BIN] = {
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 1300, 1300, 1300, 1300, 1100,
	1100, 1100, 1100, 960, 960, 960, 960
};

static const u32 threshold_ddr4[NUM_OF_DDR4_SPEED_BIN] = {
	1300, 1300, 1300, 1100, 1100, 1100, 960, 960, 960,
	850, 850, 850, 850, 770, 770, 770, 770
};

static const u32 tRFC_ddr3[5] = { 90000, 110000, 160000, 260000, 350000 };
static const u32 tRFC1_ddr4[6] = { 0, 0, 160000, 260000, 350000, 390000 };
static const u32 tRFC2_ddr4[6] = { 0, 0, 110000, 160000, 260000, 300000 };
static const u32 tRFC4_ddr4[6] = { 0, 0, 90000, 110000, 160000, 200000 };
static	const u32 tWR = 15000;

/* table saves values in ns * 1000 = psec */
static const u32 tCK[NUM_OF_DEFAULTS_FREQ] = {
		2500, 1875, 1500, 1250, 1071, 1034, 1000, 968,
		938, 909, 882, 857, 833, 750, 682, 625
};

/* table saves values in psec * 100 */
static const u32 tCK1[NUM_OF_DEFAULTS_FREQ] = {
		250000, 187500, 150000, 125000, 107150, 103448, 100000,
		96774, 93750, 90909, 88235, 85714, 83333, 75000, 68181, 62500
};

/* table saves values in pseq * 1000 */
const u32 tCK2[NUM_OF_DEFAULTS_FREQ] = {
		2500000, 1875000, 1500000, 1250000, 1071500, 1034483, 1000000,
		967742, 937500, 909091, 882353, 857143, 833333, 750000, 681818, 625000
};

const u32 trefi_x1[NUM_OF_DEFAULTS_FREQ] = {
		3120, 4160, 5200, 6240, 7279, 7540, 7800, 8060, 8320, 8580, 8840,
		9100, 9360, 10400, 11440, 12480
};

static const u32 pg_wait[NUM_OF_DEFAULTS_FREQ] = {
		1, 2, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0
};

static const u32 clk_per_row[NUM_OF_DEFAULTS_FREQ] = {
		12700000, 16933333, 21166666, 25400000, 29631358, 30691659, 31750000, 32808331,
		33866667, 34924996, 35983331, 37041660, 38100015, 42333333, 46566679, 50800000
};

#define	CL_MR0_DDR3_OFFSET	5
static const u32 cl_mr0_ddr3[12] = {
	0x2, 0x4, 0x6, 0x8, 0xa, 0xc,
	0xe, 0x1, 0x3, 0x5, 0x7, 0x9
};

#define	CL_MR0_DDR4_OFFSET	9
static const int cl_mr0_ddr4[25] = {
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xd, 0x8, 0xe, 0x9, 0xf,
	0xa, 0xc, 0xb, -1, 0x11, -1, 0x13, -1, 0x15, -1, 0x17, -1
};

#define WR_MR0_DDR3_OFFSET	5
static const u32 wr_mr0_ddr3[12] = {
	0x1, 0x2, 0x3, 0x4, 0x5, 0x5, 0x6, 0x6, 0x7, 0x7, 0x0, 0x0
};

static const u32 wr_mr0_ddr4[27] = {
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x1, 0x1, 0x2, 0x2, 0x3,
	0x3, 0x4, 0x4, 0x5, 0x5, 0x7, 0x7, 0x6,
	0x6, 0x8, 0x8
};

#define CWL_MR2_DDR3_OFFSET	5
static const u32 cwl_mr2_ddr3[8] = {
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7
};

#define CWL_MR2_DDR4_OFFSET	9
static const u32 cwl_mr2_ddr4[12] = {
	0x0, 0x1, 0x2, 0x3, 0xff, 0x4,
	0xff, 0x5, 0xff, 0x6, 0xff, 0x7
};


#define pub_read_modify_write(block, address, reg_name, field, val)\
{\
	union pub_##reg_name pub_reg;\
	pub_reg.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], address);\
	pub_reg.fields.field = val;\
	emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], address, pub_reg.reg);\
}

#ifdef VREF_PER_RANK
static union pub_vtdr res_per_byte_vtdr[4];
static union pub_dx_n_gcr5 res_dx_n_gcr5[4];
static union pub_dx_n_gcr5 dx_n_gcr5;
#endif
static union pub_mr0_ddr3 mr0_ddr3;
static union pub_mr0_ddr4 mr0_ddr4;
static union pub_mr1_ddr3 mr1_ddr3;
static union pub_mr1_ddr4 mr1_ddr4;
static union pub_mr2_ddr3 mr2_ddr3;
static union pub_mr2_ddr4 mr2_ddr4;
static union pub_mr3_ddr4 mr3_ddr4;
static union pub_mr4_ddr4 mr4_ddr4;
union pub_mr6 mr6;
union pub_mr6 mr6_rank_vref[EMEM_MC_NUM_OF_BLOCKS][2];

enum ddr_pll_freq nominal_ddr_freq[NUM_OF_DDR4_SPEED_BIN] = {
		DDR_800_MHz, DDR_800_MHz, DDR_800_MHz,
		DDR_933_MHz, DDR_933_MHz, DDR_933_MHz,
		DDR_1066_MHz, DDR_1066_MHz, DDR_1066_MHz,
		DDR_1200_MHz, DDR_1200_MHz, DDR_1200_MHz, DDR_1200_MHz,
		DDR_1333_MHz, DDR_1333_MHz, DDR_1333_MHz, DDR_1333_MHz
};

void set_pir_val(u32 pir_val) { g_ddr_training_pir_val = pir_val; }
int get_max_row_num(void)
{
	u32 density, devs, row_num = 0;

	if (IS_16X_EXT_MEM_DEVICE(current_ddr_params.type))
		devs = NUM_OF_16BIT_DEVICES;
	else
		devs = NUM_OF_8BIT_DEVICES;
	density = BYTES_TO_BITS(current_ddr_params.size /
				EMEM_MC_NUM_OF_BLOCKS /
				devs);
	if (IS_DDR4_DEVICE(current_ddr_params.type)) {
		switch (density) {
		case (2 << 10): /* 2 GBits */
			row_num = 0x3FFF;
			break;
		case (4 << 10): /* 4 GBits */
			row_num = 0x7FFF;
			break;
		case (8 << 10): /* 8 GBits */
			row_num = 0xFFFF;
			break;
		case(0x10 << 10):/* 16 GBits */
			row_num = 0x1FFFF;
			break;
		default:
			error("Invalid density value");
			row_num = 0;
			break;
		}
	} else {
		switch (density) {
		case (1<<9): /* 512 MBits */
			if(current_ddr_params.type == DDR3_8BIT)
				row_num = 0x1FFF;
			else
				row_num = 0xFFF;
			break;
		case (1 << 10): /* 1 GBits */
			if(current_ddr_params.type == DDR3_8BIT)
				row_num = 0x3FFF;
			else
				row_num = 0x1FFF;
			break;
		case (2 << 10): /* 2 GBits */
			if(current_ddr_params.type == DDR3_8BIT)
				row_num = 0x7FFF;
			else
				row_num = 0x3FFF;
			break;
		case (4 << 10): /* 4 GBits */
			if(current_ddr_params.type == DDR3_8BIT)
				row_num = 0xFFFF;
			else
				row_num = 0x7FFF;
			break;
		case (8 << 10): /* 8 GBits */
			row_num = 0xFFFF;
			break;
		default:
			error("Invalid density value");
			row_num = 0;
			break;
		}
	}

	return row_num;
}

static void configure_relax_mc(void)
{
	union emem_mc_timing1 timing1;
	union emem_mc_timing2 timing2;
	union emem_mc_timing3 timing3;
	union emem_mc_rfrsh_params_1x rfrsh_params_1x;
	union emem_mc_rfrsh_params_2x rfrsh_params_2x;
	union emem_mc_rfrsh_params_4x rfrsh_params_4x;
	union emem_mc_calib_params calib_params;
	union emem_mc_mc_mi_if mc_mi_if;
	u32 block;

	for(block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		timing1.reg = read_non_cluster_reg(emem_mc_block_id[block],
							EMEM_MC_REG_TIMING1);
		timing1.fields.wra_trp_gap = 0xFF;
		timing1.fields.rda_trp_gap = 0xFF;
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_TIMING1, timing1.reg);
		timing2.reg = 0xFFFFFFFF;
		write_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_TIMING2, timing2.reg);
		timing3.reg = read_non_cluster_reg(emem_mc_block_id[block],
							EMEM_MC_REG_TIMING3);
		timing3.fields.tfaw = 0xFF;
		timing3.fields.trc = 0xFF;
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_TIMING3, timing3.reg);

		rfrsh_params_1x.reg = read_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_RFRSH_PARAMS_1X);
		rfrsh_params_1x.fields.trfc = 0x1ff;
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_1X, rfrsh_params_1x.reg);
		rfrsh_params_2x.reg = read_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_RFRSH_PARAMS_2X);
		rfrsh_params_2x.fields.trfc = 0x1ff;
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_2X, rfrsh_params_2x.reg);
		rfrsh_params_4x.reg = read_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_RFRSH_PARAMS_4X);
		rfrsh_params_4x.fields.trfc = 0x1ff;
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_4X, rfrsh_params_4x.reg);

		calib_params.reg = read_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_CALIB_PARAMS);
		calib_params.fields.tzqcs = 0x3FF;
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_CALIB_PARAMS, calib_params.reg);
		mc_mi_if.reg = read_cached_register(MC_MI_IF_0 + block);
		mc_mi_if.fields.bank_rdy_pre = 3;
		write_cached_register(MC_MI_IF_0 + block, mc_mi_if.reg);
	}
}


static void configure_mc(void)
{
	union emem_mc_timing1 timing1;
	union emem_mc_timing2 timing2;
	union emem_mc_timing3 timing3;
	union emem_mc_rfrsh_params_1x rfrsh_params_1x;
	union emem_mc_rfrsh_params_2x rfrsh_params_2x;
	union emem_mc_rfrsh_params_4x rfrsh_params_4x;
	union emem_mc_sync1 sync1;
	union emem_mc_sync2 sync2;
	union emem_mc_calib_params calib_params;
	union emem_mc_ddr_properties ddr_properties;
	union emem_mc_mng_cmd_params mng_cmd_params;
	union emem_mc_mc_mi_if mc_mi_if;
	union emem_mc_ext_mc_latency ext_mc_latency;
	union emem_mc_mrs_rfrsh_ack mrs_rfrsh_ack;
	union emem_mc_ezref_cntr ezref_cntr;
	union emem_mc_ezref_timing ezref_timing;
	union emem_mc_phy_update phy_update;

	union crg_emi_rst emi_rst, saved_emi_rst[EMEM_MC_NUM_OF_BLOCKS];
	u32 tfaw, block, sbin, tck_index, tmp, devs, am;
	u32 density, trp, tras, trc, trfc, trfc1, trfc2, trfc4;
	u32 max_val, cwl, cl, ddr4_factor, tfaw_reduce_factor = 1, tDQSCK;
	u32 row_num = 0, twtr, reg;

	for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END ; reg++) {
		emi_rst.reg = read_non_cluster_reg(CRG_BLOCK_ID, reg);
		saved_emi_rst[reg - CRG_REG_EMI_RST_START].reg = emi_rst.reg;
		emi_rst.fields.srst_n = 0;
		emi_rst.fields.umi_ifc_rst_n = 0;
		write_non_cluster_reg(CRG_BLOCK_ID, reg, emi_rst.reg);
	}

	emi_rst.fields.grst_n = 1;
	emi_rst.fields.umi_cfg_rst_n = 1;
	for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END ; reg++)
		write_non_cluster_reg(CRG_BLOCK_ID, reg, emi_rst.reg);
	if(get_debug())
		printf("==== configure_mc ====\n");

	tck_index = current_ddr_params.pll_freq;

	sbin = current_ddr_params.speed_bin;
	if (!IS_DDR4_DEVICE(current_ddr_params.type)) {
		cwl = cwl_ddr3[tck_index][sbin];
		cl = cl_ddr3[tck_index][sbin];
		tDQSCK = tDQSCK_ddr3[sbin];
		trc = trc_ddr3[sbin];
		tras = tRAS_ddr3[sbin];
		trp = tRP_ddr3[sbin];
		ddr4_factor = 0;
		if (current_ddr_params.type == DDR3_8BIT)
			tfaw = tFAW_ddr3_x8[sbin];
		else
			tfaw = tFAW_ddr3_x16[sbin];
		ddr_properties.fields.gen = 0;
	} else {
		cwl = cwl_ddr4[tck_index][sbin];
		cl = cl_ddr4[tck_index][sbin];
		tDQSCK = tDQSCK_ddr4[sbin];
		trc = trc_ddr4[sbin];
		tras = tRAS_ddr4[sbin];
		trp = tRP_ddr4[sbin];
		ddr4_factor = 1;
		if (current_ddr_params.type == DDR4_8BIT)
			tfaw = tFAW_ddr4_x8[sbin];
		else
			tfaw = tFAW_ddr4_x16[sbin];
		ddr_properties.fields.gen = 1;
	}
	/* timing 1*/
	tmp = DDR_ROUND_UP((tWR + trp) * 1000, tCK2[tck_index]);
	timing1.fields.wra_trp_gap = DDR_ROUND_UP(cwl + cl - 1 + 4 + tmp, 2);
	max_val = max((u32)(4 * tCK[tck_index]), (u32)7500);
	tmp = DDR_ROUND_UP((trp + max_val), tCK[tck_index]);

	timing1.fields.rda_trp_gap = DDR_ROUND_UP(cl - 1 + tmp, 2);
	timing1.fields.rd_al = 2 * cl - 1 + 20;
	timing1.fields.wr_al = cwl + cl - 1;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				      EMEM_MC_REG_TIMING1, timing1.reg);
	/* timing 2*/
	if (IS_DDR4_DEVICE(current_ddr_params.type))
		twtr = max_t(u32, 2, DDR_ROUND_UP(2500, tCK[tck_index]));
	else
		twtr = max_t(u32, 4, DDR_ROUND_UP(7500, tCK[tck_index]));
	timing2.fields.wr_rd_gap_sh =
			DDR_ROUND_UP(cwl + 4 + twtr, 2) - ddr4_factor;
	twtr = max_t(u32, 4, DDR_ROUND_UP(7500, tCK[tck_index]));/*tWTR_L*/
	timing2.fields.wr_rd_gap_lo =
			DDR_ROUND_UP(cwl + 4 + twtr, 2) - ddr4_factor;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		max_val = max(current_ddr_params.brtd[block*2],
				current_ddr_params.brtd[block*2 + 1]);
		max_val = DDR_ROUND_UP(tDQSCK + max_val + 500, tCK[tck_index]);
		timing2.fields.rd_wr_gap =
				DDR_ROUND_UP((cl + 4 - cwl + 2 + max_val), 2);
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_TIMING2, timing2.reg);
	}
	/* MC_SYNC1 */
	if (IS_16X_EXT_MEM_DEVICE(current_ddr_params.type))
		devs = NUM_OF_16BIT_DEVICES;
	else
		devs = NUM_OF_8BIT_DEVICES;
	density = BYTES_TO_BITS(current_ddr_params.size /
				EMEM_MC_NUM_OF_BLOCKS /
				devs);
	if(get_debug())
		printf("density = 0x%x\n", density);
	sync1.fields.a_phase_cfg = 0x8;
	sync1.fields.d_phase_cfg = 0x2;
	sync1.fields.d_phase_cfg_th = 1;
	sync1.fields.ezref_phase_cfg = 0x12;
	sync1.fields.a_phase_en = 1;
	sync1.fields.d_phase_en = 1;
	if (IS_16X_EXT_MEM_DEVICE(current_ddr_params.type) ||
		(!IS_DDR4_DEVICE(current_ddr_params.type)
			&& density == (8 << 10)/*8Gbit*/)) {
		if (IS_DDR3_LOW_GROUP(sbin, current_ddr_params.type))
			sync1.fields.a_phase_cfg_th = 2;
		else
			sync1.fields.a_phase_cfg_th = 3;
	} else {
		if (((!IS_DDR3_LOW_GROUP(sbin, current_ddr_params.type)) &&
				(!IS_DDR4_DEVICE(current_ddr_params.type))) ||
			(IS_DDR4_LOW_GROUP(sbin, current_ddr_params.type)))
			sync1.fields.a_phase_cfg_th = 2;
		else
			sync1.fields.a_phase_cfg_th = 1;
	}
	sync1.fields.phase_skew_en = 1;
	if (((current_ddr_params.speed_bin == DDR4_2400U) ||
	   (current_ddr_params.speed_bin == DDR4_2400R) ||
	   (current_ddr_params.speed_bin == DDR4_2400P))
			&& (current_ddr_params.type ==  DDR4_16BIT))
		sync1.fields.tfaw_mult_en = 1;
	else
		sync1.fields.tfaw_mult_en = 0;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_MC_SYNC1, sync1.reg);
	/* timing3*/
	if (!IS_DDR4_DEVICE(current_ddr_params.type) &&
			sync1.fields.a_phase_cfg_th != 3)
		tfaw_reduce_factor = 2;
	timing3.fields.tfaw = DDR_ROUND_UP(DDR_ROUND_UP(
				tfaw, tCK[tck_index]), 2) - tfaw_reduce_factor;
	timing3.fields.trc =
			DDR_ROUND_UP(DDR_ROUND_UP(trc, tCK[tck_index]), 2);
	timing3.fields.dqs_rd_en = cl + cl - 1 - 4;
	timing3.fields.dqs_wr_en = cwl + cl - 1 - 2;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_TIMING3, timing3.reg);
	switch (density) {
	case 512: /* 512 MBits */
		trfc = tRFC_ddr3[0];
		trfc1 = tRFC1_ddr4[0];
		trfc2 = tRFC2_ddr4[0];
		trfc4 = tRFC4_ddr4[0];
		ddr_properties.fields.dram_size = 0;
		if (!IS_DDR4_DEVICE(current_ddr_params.type))
			row_num = 4096;
		break;
	case (1 << 10): /* 1 GBits */
		trfc = tRFC_ddr3[1];
		trfc1 = tRFC1_ddr4[1];
		trfc2 = tRFC2_ddr4[1];
		trfc4 = tRFC4_ddr4[1];
		ddr_properties.fields.dram_size = 1;
		if (!IS_DDR4_DEVICE(current_ddr_params.type))
			row_num = 8192;
		break;
	case (2 << 10): /* 2 GBits */
		trfc = tRFC_ddr3[2];
		trfc1 = tRFC1_ddr4[2];
		trfc2 = tRFC2_ddr4[2];
		trfc4 = tRFC4_ddr4[2];
		ddr_properties.fields.dram_size = 2;
		row_num = 16384;
		break;
	case (4 << 10): /* 4 GBits */
		trfc = tRFC_ddr3[3];
		trfc1 = tRFC1_ddr4[3];
		trfc2 = tRFC2_ddr4[3];
		trfc4 = tRFC4_ddr4[3];
		ddr_properties.fields.dram_size = 3;
		row_num = 32768;
		break;
	case (8 << 10): /* 8 GBits */
		trfc = tRFC_ddr3[4];
		trfc1 = tRFC1_ddr4[4];
		trfc2 = tRFC2_ddr4[4];
		trfc4 = tRFC4_ddr4[4];
		ddr_properties.fields.dram_size = 4;
		row_num = 65536;
		break;
	case(0x10 << 10):/* 16 GBits */
		row_num = 131072;
		trfc = 0;
		trfc1 = tRFC1_ddr4[5];
		trfc2 = tRFC2_ddr4[5];
		trfc4 = tRFC4_ddr4[5];
		break;
	default:
		trfc = tRFC_ddr3[0];
		trfc1 = tRFC1_ddr4[0];
		trfc2 = tRFC2_ddr4[0];
		trfc4 = tRFC4_ddr4[0];
		ddr_properties.fields.dram_size = 5;
		row_num = 65536;
		break;
	}
	if (!IS_16X_EXT_MEM_DEVICE(current_ddr_params.type) &&
	    !IS_DDR4_DEVICE(current_ddr_params.type))
		row_num = min((u32)row_num*2, (u32)65536);
	if (!IS_DDR4_DEVICE(current_ddr_params.type))
		trfc1 = trfc;
	rfrsh_params_1x.fields.trefi = trefi_x1[tck_index] / 2;
	rfrsh_params_1x.fields.trfc =
			DDR_ROUND_UP(DDR_ROUND_UP(trfc1 * 1000, tCK2[tck_index])
									, 2);
	rfrsh_params_1x.fields.rfrsh_burst = 1;
	rfrsh_params_1x.fields.mode = 0;

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_1X, rfrsh_params_1x.reg);
	rfrsh_params_2x.fields.rfrsh_burst = 1;
	if (IS_DDR4_DEVICE(current_ddr_params.type)) {
		rfrsh_params_2x.fields.trefi = trefi_x1[tck_index] / 4;
		rfrsh_params_2x.fields.trfc =
			DDR_ROUND_UP(DDR_ROUND_UP(trfc2 * 1000, tCK2[tck_index])
									, 2);
	} else {
		rfrsh_params_2x.fields.trefi =
				DDR_ROUND_UP((7800000 / tCK[tck_index]), 2);
		rfrsh_params_2x.fields.trfc =
			DDR_ROUND_UP(DDR_ROUND_UP(trfc, tCK[tck_index]), 2);
	}
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_2X, rfrsh_params_2x.reg);
	if (IS_DDR4_DEVICE(current_ddr_params.type)) {
		rfrsh_params_4x.fields.trefi = trefi_x1[tck_index] / 8;
		rfrsh_params_4x.fields.trfc =
			DDR_ROUND_UP(DDR_ROUND_UP(trfc4 * 1000,
							tCK2[tck_index]), 2);
	} else {
		rfrsh_params_4x.fields.trefi =
			DDR_ROUND_UP((7800000 / tCK[tck_index]), 2);
		rfrsh_params_4x.fields.trfc =
			DDR_ROUND_UP(DDR_ROUND_UP(trfc, tCK[tck_index]), 2);
	}
	rfrsh_params_4x.fields.rfrsh_burst = 1;

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_RFRSH_PARAMS_4X, rfrsh_params_4x.reg);
	calib_params.fields.th = 100;
	calib_params.fields.debug_mode = 0;
	if (IS_DDR4_DEVICE(current_ddr_params.type))
		calib_params.fields.tzqcs = 64;
	else
		calib_params.fields.tzqcs = max_t(u32, (64000 * tCK[tck_index]),
								80000);
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_CALIB_PARAMS, calib_params.reg);
	ddr_properties.fields.work_mode = 0;
	if (IS_16X_EXT_MEM_DEVICE(current_ddr_params.type))
		ddr_properties.fields.dq_pins = 0x2;
	else
		ddr_properties.fields.dq_pins = 0x1;

	if (row_num < 8192)
		ddr_properties.fields.page_num = 0;
	else if (row_num < 16384)
		ddr_properties.fields.page_num = 1;
	else if (row_num < 32768)
		ddr_properties.fields.page_num = 2;
	else if (row_num < 65536)
		ddr_properties.fields.page_num = 3;
	else if (row_num < 131072)
		ddr_properties.fields.page_num = 4;
	else
		ddr_properties.fields.page_num = 5;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_DDR_PROPERTIES, ddr_properties.reg);
	mng_cmd_params.reg = 0x178;
	mng_cmd_params.fields.ras_cas_if = 0xA;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_MNG_CMD_PARAMS, mng_cmd_params.reg);
	mc_mi_if.fields.ack_addr_gap = 4;
	mc_mi_if.fields.arb_algorithm = 1;
	mc_mi_if.fields.ack_data_gap = 3;
	mc_mi_if.fields.chosen_mi = 0;
	mc_mi_if.fields.ret_data_gap = 1;
	mc_mi_if.fields.out_of_order = 1;
	mc_mi_if.fields.swap_priority_on_prefare_long = 1;
	mc_mi_if.fields.swap_priority_on_starve_for_short = 1;
	mc_mi_if.fields.swap_priority_on_starve_for_long = 1;
	mc_mi_if.fields.cmd_switch_th = 0x7F;

	if (current_ddr_params.type == DDR4_8BIT)
		mc_mi_if.fields.mc_highest_bank = 0xf;
	else
		mc_mi_if.fields.mc_highest_bank = 0x7;

	if (current_ddr_params.pll_freq < nominal_ddr_freq[current_ddr_params.speed_bin])
		mc_mi_if.fields.bank_rdy_pre = 3;
	else {
		/* optimization of writing to ddr bank
		 * removed due to refreshing problems */
		u32 bank_rdy_pre_rff = 0;
		if ((IS_DDR4_DEVICE(current_ddr_params.type)
		     && (tCK[tck_index] > threshold_ddr4[sbin])) ||
		   (!IS_DDR4_DEVICE(current_ddr_params.type)
		     && (tCK[tck_index] > threshold_ddr3[sbin])))
				bank_rdy_pre_rff = 1;
		if (IS_DDR4_DEVICE(current_ddr_params.type)) {
			if (sbin <= DDR4_1600L)
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 5 - bank_rdy_pre_rff;
			else if (sbin <= DDR4_1866N)
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 5 - bank_rdy_pre_rff;
			else
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 4 - bank_rdy_pre_rff;
			if(IS_DDR4_2400_GROUP(current_ddr_params.speed_bin) &&
				(current_ddr_params.type == DDR4_16BIT) &&
				(mc_mi_if.fields.bank_rdy_pre >= 2))
				mc_mi_if.fields.bank_rdy_pre -= 2;
		} else {
			if (sbin <= DDR3_1333J)
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 3 - bank_rdy_pre_rff;
			else if (sbin <= DDR3_1600K)
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 6 - bank_rdy_pre_rff;
			else if (sbin <= DDR3_1866M)
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 5 - bank_rdy_pre_rff;
			else
				mc_mi_if.fields.bank_rdy_pre =
				mc_mi_if.fields.ack_addr_gap + 4 - bank_rdy_pre_rff;
		}

	}

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_cached_register(MC_MI_IF_0 + block, mc_mi_if.reg);
	/* sync2 */
	sync2.fields.mi_ack_phase = 0x8;
	sync2.fields.ezref_phase = 0x2;
	sync2.fields.use_ezref_phase = 0;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_MC_SYNC2, sync2.reg);
	/*ext_mc_latency*/
	ext_mc_latency.fields.ext_mc_rl1 = 13;
	ext_mc_latency.fields.ext_mc_rl0 = 13;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_cached_register((EXT_MC_LATENCY_0 + block), ext_mc_latency.reg);
	/* mrs_rfrsh_ack */
	mrs_rfrsh_ack.fields.phase0 =
			((GET_BITS(sync1.fields.a_phase_cfg, 0, 3) % 8) +
			mc_mi_if.fields.ack_addr_gap + 2)
			% (sync1.fields.a_phase_cfg_th + 1);
	mrs_rfrsh_ack.fields.phase1 =
			((GET_BITS(sync1.fields.a_phase_cfg, 3, 3) % 8) +
			mc_mi_if.fields.ack_addr_gap + 2)
			% (sync1.fields.a_phase_cfg_th + 1);
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_MRS_RFRSH_ACK, mrs_rfrsh_ack.reg);

	/* mc_addr_mirror */
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		am = (current_ddr_params.address_mirror[2 * block]) +
		((current_ddr_params.address_mirror[(2 * block) + 1]) << 1);
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_MC_ADDR_MIRROR, am);
	}
	ezref_cntr.fields.th1 = row_num / 635;
	ezref_cntr.fields.th2 = (5 * row_num) / 635;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_EZREF_CNTR, ezref_cntr.reg);
	/* ezref_timing*/
	ezref_timing.fields.tras = DDR_ROUND_UP(DDR_ROUND_UP(tras * 100,
						tCK1[tck_index]), 2) + 6;
	ezref_timing.fields.tpre = DDR_ROUND_UP(DDR_ROUND_UP(trp * 100,
						tCK1[tck_index]), 2);
	ezref_timing.fields.trefi = clk_per_row[tck_index] / row_num;
	ezref_timing.fields.sectors_debug_mode = 0;
	if (!IS_DDR4_DEVICE(current_ddr_params.type))
		ezref_timing.fields.mask_bg_req = 2;
	else {
		if (sbin >= DDR4_2666T)
			ezref_timing.fields.mask_bg_req = 4;
		else
			ezref_timing.fields.mask_bg_req = 2;
	}
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
				EMEM_MC_REG_EZREF_TIMING, ezref_timing.reg);

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		phy_update.reg = read_cached_register(PHY_UPDATE_0 + block);
		phy_update.fields.maxpw = 4;
		write_cached_register(PHY_UPDATE_0 + block, phy_update.reg);
	}
	configure_emem_mi();
	emi_rst.fields.umi_ifc_rst_n = 1;
	emi_rst.fields.umi_phy_rst_n = 1;
	for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END ; reg++) {
		emi_rst.fields.srst_n = saved_emi_rst[reg - CRG_REG_EMI_RST_START].fields.srst_n;
		write_non_cluster_reg(CRG_BLOCK_ID, reg, emi_rst.reg);
	}
}

void configure_crg_emi_rst(enum crg_emi_rst_phase phase)
{
	int reg;
	unsigned int value, bitmask = 0;

	switch (phase) {
	case EMI_RST_PHASE_1:
		bitmask |= CRG_EMI_RST_GRST_N;
		bitmask |= CRG_EMI_RST_UMI_CFG_RST_N;
		break;
	case EMI_RST_PHASE_2:
		bitmask |= CRG_EMI_RST_UMI_PHY_RST_N;
		bitmask |= CRG_EMI_RST_UMI_IFC_RST_N;
		break;
	case EMI_RST_PHASE_3:
		bitmask |= CRG_EMI_RST_SRST_N;
		break;
	default:
		break;
	}

	for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END ; reg++) {
		value = read_non_cluster_reg(CRG_BLOCK_ID, reg);
		value |= bitmask;
		write_non_cluster_reg(CRG_BLOCK_ID, reg, value);
	}
}

int apb_ifc_rst(void)
{
	u32 block, value, test_data_0, test_data_1;
	u32 seed = 0x11;
	union emem_mc_apb_ifc	apb_ifc;

	if(get_debug())
		printf("==== apb_ifc_rst ====\n");

	srand(seed);
	apb_ifc.reg = 0x04000000;
	apb_ifc.fields.cont_rst = 1;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_APB_IFC, apb_ifc.reg);
	}
	udelay(5);
	/* set clk to core/4 by setting cfg_clk_div to 0*/
	apb_ifc.fields.cont_clk_div_en = 1;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_APB_IFC, apb_ifc.reg);
	}
	udelay(5);
	apb_ifc.fields.cont_clk_en = 1;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_APB_IFC, apb_ifc.reg);
	}
	udelay(5);
	apb_ifc.fields.cont_rst = 0;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_APB_IFC, apb_ifc.reg);

	/* check result */
	for (block = 0; block < EMEM_MI_NUM_OF_BLOCKS; block++) {
		value = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_RIDR_REG_ADDR);
		if (value != 0x00250231 && !g_EZsim_mode) {
			error("RIDR register value is not as expected");
			return -EBUSY;
		}
		test_data_0 = rand() & 0xFFFFFFF0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
					PUB_GPR0_REG_ADDR, test_data_0);
		test_data_1 = rand() & 0xFFFFFFF0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
				PUB_GPR1_REG_ADDR, test_data_1);
		value = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_GPR0_REG_ADDR);
		if (value != test_data_0 && !g_EZsim_mode) {
			error("GPR0 register value is not as expected");
			return -EBUSY;
		}
		value = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_GPR1_REG_ADDR);
		if (value != test_data_1 && !g_EZsim_mode) {
			error("GPR1 register value is not as expected");
			return -EBUSY;
		}
	}

	return 0;
}

u32 get_optimal_vref_index(const u32 *table_1, const u32 *table_2,
			u32 size_1, u32 size_2, u32 vref_prec, u32 *index)
{
	u32 sdram_r1_vref_index, sdram_r2_vref_index;
	u32 sdram_r1_vref_margin, sdram_r2_vref_margin;

	sdram_r1_vref_index  = get_vref_index(vref_dq_1, size_1, vref_prec);
	sdram_r2_vref_index  = get_vref_index(vref_dq_2, size_2, vref_prec);
	sdram_r1_vref_margin = min((size_1 - sdram_r1_vref_index), sdram_r1_vref_index);
	sdram_r2_vref_margin = min((size_2 - sdram_r2_vref_index), sdram_r2_vref_index);
	if(sdram_r1_vref_margin > sdram_r2_vref_margin) {
		*index = sdram_r1_vref_index;
		return 0;
	} else {
		*index = sdram_r2_vref_index;
		return 1;
	}
}

void	configure_phy(void)
{
	u32 ddrmd, frqsel, tck_index, block, asym_drv_en, pu_odt_only;
	u32 dis_non_lin_comp, zq_ocdi, zq_odt_ddr3, zq_odt_ddr4, zq_div_0;
	u32 zprog_pu_odt_only, zprog_asym_drv_pd, zprog_asym_drv_pu, trp, sbin;
	u32 i, tmod, trfc, tcke, tdllk, txp, twlo, trcd, trc, density;
	u32 cwl, index, tras, trtp, tfaw, trrd, zq_div_1, vref_prec;
	u32 vref_idx = 0, cl_mr0_ddr4_idx;
	union pub_zqcr zqcr;
	union pub_zqxpr zqxpr;

	/*union pub_dx_n_gcr4 dx_n_gcr4;*/

	if(get_debug())
		printf("==== configure_phy ====\n");
	tck_index = current_ddr_params.pll_freq;
	sbin = current_ddr_params.speed_bin;
	if (tCK[tck_index] < 1075)
		frqsel = 0;
	else if (tCK[tck_index] < 1818)
		frqsel = 1;
	else
		frqsel = 2;
	switch (current_ddr_params.phy_wr_drv) {
	case	RZQ_DIV_4:
		zq_ocdi = 0x7;
		break;
	case	RZQ_DIV_5:
		zq_ocdi = 0x9;
		break;
	case	RZQ_DIV_6:
		zq_ocdi = 0xb;
		break;
	case	RZQ_DIV_7:
		zq_ocdi = 0xd;
		break;
	case	RZQ_DIV_8:
		zq_ocdi = 0xf;
		break;
	default:
		zq_ocdi = 0xd;
		break;
	}

	switch (current_ddr_params.phy_rd_odt) {
	case	RZQ_DIV_2:
		zq_odt_ddr3 = 0x2;
		zq_odt_ddr4 = 0x3;
		break;
	case	RZQ_DIV_3:
		zq_odt_ddr4 = 0x5;
		zq_odt_ddr3 = 0x5;
		break;
	case	RZQ_DIV_4:
		zq_odt_ddr3 = 0x5;
		zq_odt_ddr4 = 0x7;
		break;
	case	RZQ_DIV_5:
		zq_odt_ddr4 = 0x9;
		zq_odt_ddr3 = 0x5;
		break;
	case	RZQ_DIV_6:
		zq_odt_ddr3 = 0x8;
		zq_odt_ddr4 = 0xb;
		break;
	case	RZQ_DIV_7:
		zq_odt_ddr4 = 0xd;
		zq_odt_ddr3 = 0x5;
		break;
	case	RZQ_DIV_8:
		zq_odt_ddr4 = 0xf;
		zq_odt_ddr3 = 0x5;
		break;
	default:
		zq_odt_ddr3 = 0x5;
		zq_odt_ddr4 = 0x9;
		break;
	}
	if (IS_16X_EXT_MEM_DEVICE(current_ddr_params.type))
		density = BYTES_TO_BITS(current_ddr_params.size /
					EMEM_MC_NUM_OF_BLOCKS /
					NUM_OF_16BIT_DEVICES);
	else
		density = BYTES_TO_BITS(current_ddr_params.size /
					EMEM_MC_NUM_OF_BLOCKS /
					NUM_OF_8BIT_DEVICES);
	index = 0;
	for (i = 9; i <= 13; i++) {
		if (GET_BIT(density, i)) {
			index = i-9;
			break;
		}
	}
	mr0_ddr4.reg = 0;
	mr0_ddr3.reg = 0;
	if (IS_DDR4_DEVICE(current_ddr_params.type)) {
		ddrmd = 0x4;
		asym_drv_en = 1;
		pu_odt_only = 1;
		dis_non_lin_comp = 0;
		zq_div_0 = (16 * zq_odt_ddr4) + zq_ocdi;
		zq_div_1 = zq_div_0;
		zprog_pu_odt_only = zq_odt_ddr4;
		zprog_asym_drv_pd = zq_ocdi;
		zprog_asym_drv_pu = zq_ocdi;
		trp = tRP_ddr4[sbin];
		tras = tRAS_ddr4[sbin];
		tmod = max_t(u32, (24 * tCK[tck_index]), 15000);
		tcke = tcke_ddr4[sbin];
		trfc = tRFC1_ddr4[index];
		if(current_ddr_params.speed_bin < DDR4_2133N)
			tdllk = 597;
		else if(current_ddr_params.speed_bin < DDR4_2666T)
			tdllk = 768;
		else
			tdllk = 854;
		txp = txp_ddr4[sbin];
		twlo = twlo_ddr4[sbin];
		trcd = trcd_ddr4[sbin];
		trc = trc_ddr4[sbin];
		cl_mr0_ddr4_idx =
				cl_ddr4[tck_index][sbin] - CL_MR0_DDR4_OFFSET;
		if (cl_mr0_ddr4_idx < 25) {
			if(cl_mr0_ddr4[cl_mr0_ddr4_idx] != -1) {
				mr0_ddr4.fields.cl2 =
					GET_BIT(cl_mr0_ddr4[cl_mr0_ddr4_idx], 0);
				mr0_ddr4.fields.cl4_6 =
					GET_BITS((cl_mr0_ddr4[cl_mr0_ddr4_idx]), 1, 3);
				mr0_ddr4.fields.cl12 =
					GET_BIT(cl_mr0_ddr4[cl_mr0_ddr4_idx], 4);
			}
			else {
				error("cl illegal value\n");
				return;
			}
		} else {
			error("cl illegal value\n");
			return;
		}
		mr0_ddr4.fields.wr9_11 = GET_BITS(wr_mr0_ddr4[DDR_ROUND_UP(tWR * 1000,
						tCK2[tck_index])], 0, 3);
		mr0_ddr4.fields.wr13 = GET_BIT(wr_mr0_ddr4[DDR_ROUND_UP(tWR * 1000,
						tCK2[tck_index])], 3);
		cwl = cwl_mr2_ddr4[cwl_ddr4[tck_index][sbin]
						- CWL_MR2_DDR4_OFFSET];
		if (current_ddr_params.type == DDR4_16BIT) {
			trrd =  trrd_2kb_ddr4[sbin];
			tfaw = tFAW_ddr4_x16[sbin];
		} else {
			trrd =  trrd_1kb_ddr4[sbin];
			tfaw = tFAW_ddr4_x8[sbin];
		}
	} else {
		ddrmd = 0x3;
		asym_drv_en = 0;
		pu_odt_only = 0;
		dis_non_lin_comp = 1;
		zq_div_0 = 0x70 + zq_ocdi;
		zq_div_1 = (16 * zq_odt_ddr3) + zq_ocdi;
		zprog_pu_odt_only = 0x7;
		zprog_asym_drv_pd = 0xb;
		zprog_asym_drv_pu = 0xb;
		trp = tRP_ddr3[sbin];
		tras = tRAS_ddr3[sbin];
		tmod = max_t(u32, (12 * tCK[tck_index]), 15000);
		trfc = tRFC_ddr3[index];
		tcke = tcke_ddr3[sbin];
		tdllk = 512;
		txp = txp_ddr3[sbin];
		twlo = twlo_ddr3[sbin];
		trcd = trcd_ddr3[sbin];
		trc = trc_ddr3[sbin];
		mr0_ddr3.fields.cl2 = GET_BIT(
			cl_mr0_ddr3[cl_ddr3[tck_index][sbin]
						- CL_MR0_DDR3_OFFSET], 0);
		mr0_ddr3.fields.cl4_6 = GET_BITS(
			(cl_mr0_ddr3[cl_ddr3[tck_index][sbin]
						- CL_MR0_DDR3_OFFSET]), 1, 3);
		mr0_ddr3.fields.wr = wr_mr0_ddr3[DDR_ROUND_UP(tWR,
							tCK[tck_index])];
		cwl = cwl_mr2_ddr3[cwl_ddr3[tck_index][sbin] -
							CWL_MR2_DDR3_OFFSET];
		if ((density == (8 << 10)) ||
				(current_ddr_params.type == DDR3_16BIT)) {
			trrd = trrd_2kb_ddr3[sbin];
			tfaw = tFAW_ddr3_x16[sbin];
		} else {
			trrd = trrd_1kb_ddr3[sbin];
			tfaw = tFAW_ddr3_x8[sbin];
		}
	}
	trtp = max_t(u32, (4 * tCK[tck_index]), 7500);
	mr1_ddr3.reg = 0;
	mr1_ddr4.reg = 0;
	mr2_ddr3.reg = 0;
	mr2_ddr4.reg = 0;
	mr3_ddr4.reg = 0;
	mr4_ddr4.reg = 0;
	 /* DLL off for frequency less than 125 MHZ */
	if (current_ddr_params.clock_frequency <= (1250))
		mr1_ddr3.fields.dll = 1;

	mr1_ddr4.fields.dll_en = 1;
	if (current_ddr_params.mem_rtt_nom == RZQ_DIV_4) {
		mr1_ddr3.fields.rtt_nom_2 = 1;
		mr1_ddr4.fields.rtt_nom = 1;
	} else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_2) {
		mr1_ddr3.fields.rtt_nom_6 = 1;
		mr1_ddr4.fields.rtt_nom = 2;
	} else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_6) {
		mr1_ddr3.fields.rtt_nom_2 = 1;
		mr1_ddr3.fields.rtt_nom_6 = 1;
		mr1_ddr4.fields.rtt_nom = 3;
	} else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_12) {
		mr1_ddr3.fields.rtt_nom_9 = 1;
	} else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_8) {
		mr1_ddr3.fields.rtt_nom_9 = 1;
		mr1_ddr3.fields.rtt_nom_2 = 1;
	} else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_1)
		mr1_ddr4.fields.rtt_nom = 4;
	else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_5)
		mr1_ddr4.fields.rtt_nom = 5;
	else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_3)
		mr1_ddr4.fields.rtt_nom = 6;
	else if (current_ddr_params.mem_rtt_nom == RZQ_DIV_7)
		mr1_ddr4.fields.rtt_nom = 7;

	if (current_ddr_params.mem_dic == RZQ_DIV_7)
		mr1_ddr3.fields.dic_1 = 1;
	if (current_ddr_params.mem_dic == RZQ_DIV_5)
		mr1_ddr4.fields.odic = 1;
	mr1_ddr4.fields.al = 1;
	mr1_ddr3.fields.al = 1;

	mr2_ddr3.fields.cwl = cwl;
	mr2_ddr3.fields.asr = (u32)current_ddr_params.asr_en;

	mr2_ddr4.fields.cwl = cwl;
	if (current_ddr_params.asr_en) {
		mr2_ddr4.fields.lpasr = 0x3;
	} else if (current_ddr_params.tcase > 85) {
		mr2_ddr4.fields.lpasr = 0x2;
		mr2_ddr3.fields.srt = 1;
	}
	if (current_ddr_params.mem_rtt_wr == RZQ_DIV_4)
		mr2_ddr3.fields.rtt_wr = 1;
	else if (current_ddr_params.mem_rtt_wr == RZQ_DIV_2) {
		mr2_ddr3.fields.rtt_wr = 2;
		mr2_ddr4.fields.rtt_wr = 1;
	} else if (current_ddr_params.mem_rtt_wr == RZQ_DIV_1)
		mr2_ddr4.fields.rtt_wr = 2;
	else if (current_ddr_params.mem_rtt_wr == RZQ_DIV_3)
		mr2_ddr4.fields.rtt_wr = 4;
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		pub_dual_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, dual_chn, 1, updmstrc0, 1);
		pub_read_modify_write(block, PUB_PLLCR_REG_ADDR, pllcr, frq_sel, frqsel);
		pub_dual_read_modify_write(block, PUB_DXCCR_REG_ADDR, dxccr, dqsres, 0, dqsnres, 0x8);
		pub_dual_read_modify_write(block, PUB_DSGCR_REG_ADDR, dsgcr, puren, 0, ctlzuen, 1);
		pub_read_modify_write(block, PUB_DCR_REG_ADDR, dcr, ddrmd, ddrmd);
		pub_dual_read_modify_write(block, PUB_DTCR0_REG_ADDR, dtcr0, dtmpr, 1, dtwbddm, 0);
		pub_read_modify_write(block, PUB_DTCR0_REG_ADDR, dtcr0, dtcmpd, 1);
		pub_dual_read_modify_write(block, PUB_VTCR1_REG_ADDR, vtcr1, shren, 1, hvio, 1);
		pub_dual_read_modify_write(block, PUB_DTPR0_REG_ADDR, dtpr0, trtp, DDR_ROUND_UP(trtp * 1000, tCK2[tck_index]) , trp, DDR_ROUND_UP(trp * 1000, tCK2[tck_index]));
		pub_dual_read_modify_write(block, PUB_DTPR0_REG_ADDR, dtpr0, tras, DDR_ROUND_UP(tras * 1000, tCK2[tck_index]) , trrd, DDR_ROUND_UP(trrd * 1000, tCK2[tck_index]));
		pub_dual_read_modify_write(block, PUB_DTPR1_REG_ADDR, dtpr1, tmrd, 0, tmod, tmod);
		pub_dual_read_modify_write(block, PUB_DTPR1_REG_ADDR, dtpr1, tfaw, DDR_ROUND_UP(tfaw, tCK[tck_index]), twlmrd, 40);
		pub_dual_read_modify_write(block, PUB_DTPR2_REG_ADDR, dtpr2, txs, max_t(u32, DDR_ROUND_UP((trfc + 10000) * 100, tCK1[tck_index]), tdllk), tcke, DDR_ROUND_UP(tcke, tCK[tck_index]));
		pub_dual_read_modify_write(block, PUB_DTPR2_REG_ADDR, dtpr2, trtodt, 0, trtw, 0);
		pub_dual_read_modify_write(block, PUB_DTPR3_REG_ADDR, dtpr3, tdqsck, 0, tdqsck_max, 0);
		pub_dual_read_modify_write(block, PUB_DTPR3_REG_ADDR, dtpr3, tdllk, tdllk, tccd, 0);
		pub_read_modify_write(block, PUB_DTPR3_REG_ADDR, dtpr3, tofdx, 0);
		pub_dual_read_modify_write(block, PUB_DTPR4_REG_ADDR, dtpr4, txp, DDR_ROUND_UP(txp, tCK[tck_index]), twlo, DDR_ROUND_UP(twlo, tCK[tck_index]));
		pub_read_modify_write(block, PUB_DTPR4_REG_ADDR, dtpr4, trfc, DDR_ROUND_UP(trfc * 1000, tCK2[tck_index]));
		pub_dual_read_modify_write(block, PUB_DTPR5_REG_ADDR, dtpr5, twtr, max_t(u32, 4, DDR_ROUND_UP(7500000, tCK2[tck_index])), trcd, DDR_ROUND_UP(trcd * 1000, tCK2[tck_index]));
		pub_read_modify_write(block, PUB_DTPR5_REG_ADDR, dtpr5, trc, DDR_ROUND_UP(trc * 1000, tCK2[tck_index]));
		dx_n_gcr5.reg = 0;
		dx_n_gcr5.fields.dxrefiselr2 = 9;
		dx_n_gcr5.fields.dxrefiselr3 = 9;
		if (IS_DDR4_DEVICE(current_ddr_params.type)) {
			vref_prec = (current_ddr_params.phy_rd_vref * 10000)
							/ MEM_VREF_MAX_VAL;
			for (vref_idx = 0; vref_idx < 64; vref_idx++)
				if (vref_prec < phy_rd_vref_dq[vref_idx])
					break;
			if ((vref_prec - phy_rd_vref_dq[vref_idx - 1]) <
				(phy_rd_vref_dq[vref_idx] - vref_prec))
				vref_idx--;
			dx_n_gcr5.fields.dxrefiselr0 = vref_idx;
			dx_n_gcr5.fields.dxrefiselr1 = vref_idx;
		} else {
			dx_n_gcr5.fields.dxrefiselr0 = 9;
			dx_n_gcr5.fields.dxrefiselr1 = 9;
		}

		for (index = 0; index < 4; index++) {
			emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[block],
				PUB_DX0GCR5_REG_ADDR + (index * 0x40),
				dx_n_gcr5.reg);
		}

		if (IS_DDR4_DEVICE(current_ddr_params.type)) {
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR0_REG_ADDR, mr0_ddr4.reg);
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR1_REG_ADDR, mr1_ddr4.reg);
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR2_REG_ADDR, mr2_ddr4.reg);
		} else {
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR0_REG_ADDR, mr0_ddr3.reg);
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR1_REG_ADDR, mr1_ddr3.reg);
			emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[block], PUB_MR2_REG_ADDR, mr2_ddr3.reg);
		}
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_MR3_REG_ADDR, 0);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_MR4_REG_ADDR, 0);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_MR5_REG_ADDR, 0);
/*
		vref_prec = (current_ddr_params.mem_vref * 10000)
							/ MEM_VREF_MAX_VAL;
		for (vref_idx = 0; vref_idx < 51; vref_idx++)
			if (vref_prec < vref_dq_2[vref_idx])
				break;
		if ((vref_prec - vref_dq_2[vref_idx - 1]) <
					(vref_dq_2[vref_idx] - vref_prec))
			vref_idx--;
*/
		vref_prec = (current_ddr_params.mem_vref * 10000) / MEM_VREF_MAX_VAL;
		mr6.reg = 0;
		if (IS_DDR4_DEVICE(current_ddr_params.type)) {
			mr6.fields.vrefdq_tr_rng = get_optimal_vref_index(
							vref_dq_1,
							vref_dq_2,
							ARRAY_SIZE(vref_dq_1),
							ARRAY_SIZE(vref_dq_2),
							vref_prec,
							&vref_idx);
			mr6.fields.vrefdq_tr_val = vref_idx;
			if(get_debug())
				printf("index %d range %d\n",
						vref_idx, mr6.fields.vrefdq_tr_rng);
			if (tCK[tck_index] <= 1250) {
				if (IS_DDR4_1600_GROUP(sbin) ||
						IS_DDR4_1866_GROUP(sbin))
					mr6.fields.tccd_l = 1;
				else if (IS_DDR4_2133_GROUP(sbin))
					mr6.fields.tccd_l = 2;
				else if (IS_DDR4_2400_GROUP(sbin))
					mr6.fields.tccd_l = 2;
				else if (IS_DDR4_2666_GROUP(sbin))
					mr6.fields.tccd_l = 3;
				else
					mr6.fields.tccd_l = 4;
			}
		}
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_MR6_REG_ADDR, mr6.reg);

		zqcr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_ZQCR_REG_ADDR);

		zqcr.fields.pg_wait = pg_wait[tck_index];
		zqcr.fields.asym_drv_en = asym_drv_en;
		zqcr.fields.pu_odt_only = pu_odt_only;
		zqcr.fields.dis_mon_lin_comp = dis_non_lin_comp;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_ZQCR_REG_ADDR, zqcr.reg);
		udelay(1000); /**** WA 1 ***/
		zqxpr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_ZQ0PR_REG_ADDR);
		zqxpr.fields.zqdiv = zq_div_0;
		zqxpr.fields.zprog_pu_odt_only = zprog_pu_odt_only;
		zqxpr.fields.zprog_asym_drv_pd = zprog_asym_drv_pd;
		zqxpr.fields.zprog_asym_drv_pu = zprog_asym_drv_pu;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_ZQ0PR_REG_ADDR, zqxpr.reg);
		udelay(1000); /**** WA 1 ***/
		zqxpr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[block], PUB_ZQ1PR_REG_ADDR);
		zqxpr.fields.zqdiv = zq_div_1;
		zqxpr.fields.zprog_pu_odt_only = zprog_pu_odt_only;
		zqxpr.fields.zprog_asym_drv_pd = zprog_asym_drv_pd;
		zqxpr.fields.zprog_asym_drv_pu = zprog_asym_drv_pu;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_ZQ1PR_REG_ADDR, zqxpr.reg);
		udelay(1000); /**** WA 1 ***/
	}
}

/* External Memory Interface */
static void configure_emem_mi(void)
{
	int i, block_id;
	u32 val;

	if(get_debug())
		printf("configure_emem_mi\n");
#if 0
	for (i = 0; i < EMEM_MI_NUM_OF_BLOCKS ; i++) {
		write_non_cluster_reg(emem_mi_block_id[i],
				EMEM_MI_REG_DRAM_ECC, 0x11);
	}
#endif
	for (i = 0; i < EMEM_MI_NUM_OF_BLOCKS ; i++) {
		write_non_cluster_reg(emem_mi_block_id[i],
				EMEM_MI_REG_RANK_OVERRIDE, 0xD);
	}

	if (IS_DDR4_DEVICE(current_ddr_params.type))
		if(IS_16X_EXT_MEM_DEVICE(current_ddr_params.type))
			val = 0x1;
		else
			val = 0x11;
	else
		val = 0;

	for (i = 0; i < EMEM_MI_NUM_OF_BLOCKS ; i++) {
		block_id = emem_mi_block_id[i];

		write_non_cluster_reg(block_id,
				EMEM_MI_REG_CFG, 0x86B14143);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_DEVICE_PROPERTIES, val);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_RD_SORT_THR_3, 0x6419);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_RD_SORT_THR_7, 0xC846);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_RD_SORT_THR_8, 0x6423);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_WR_SORT_THR_3, 0x6149);
		write_non_cluster_reg(block_id,
				EMEM_MI_REG_WR_SORT_DATA_THR_3, 0xD80064);
		if (IS_DDR4_DEVICE(current_ddr_params.type))
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_WR_RD_REQ_SM, 0x8309E09E);
		if(IS_16X_EXT_MEM_DEVICE(current_ddr_params.type)) {
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_AVG_LAT_CALC, 0x7b4e0000);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RD_REQ_SM, 0x2305e001);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_SM_RD_WR_WIN, 0xb464F078);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_0, 0x01ff0144);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_1, 0x01ff0158);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_2, 0x01ff016c);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_3, 0x01f00180);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_4, 0x01dc0194);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_5, 0x01a401cc);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_6, 0x017c01F4);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RDWR_SM_PROFILE_7, 0x015401FF);
			write_non_cluster_reg(block_id,
				EMEM_MI_REG_RD_WR_SM_STARVATION_PREVENTION, 0x03E81770);
		}
	}
}

static int check_indirect_status(unsigned int block_id)
{
	int i;
	unsigned int value;

	/* read indirect status until ready bit is on */
	for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
		value = read_non_cluster_reg(block_id, IND_STS_REG_ADDR);
		if (value & IND_STS_RDY)
			break;
	}

	if (i == INDIRECT_RETRY_NUM) {
		error("indirect status retry number exceeded");
		return -EBUSY;
	}

	/* check if success bit is on */
	value = read_non_cluster_reg(block_id, IND_STS_REG_ADDR);
	if (!(value & IND_STS_SUCCESS)) {
		error("indirect status result was failure");
		return -EIO;
	}

	return 0;
}

u32 emem_mc_read_indirect_reg(u32 block_id, u32 address, u32 cmd, bool trace_off)
{
	bool saved_debug = false;
	u32 read_val;

	if (trace_off) {
		saved_debug = get_debug();
		set_debug(false);
	}
	write_non_cluster_reg(block_id, EMEM_MC_REG_IND_ADDR, address);
	write_non_cluster_reg(block_id, EMEM_MC_REG_IND_CMD, cmd);

	if (check_indirect_status(block_id) < 0)
		error("indirect register=0x%x write failed on block=0x%x",
				address, block_id);
	read_val = read_non_cluster_reg(block_id, EMEM_MC_REG_IND_DATA0);
	if (trace_off && saved_debug) {
		set_debug(true);
	}

	return read_val;
}

void emem_mc_write_indirect_reg(unsigned int block_id,
		unsigned int address, unsigned int data_size,
		unsigned int data_0, unsigned int data_1,
		unsigned int cmd)
{
	int i;

	for (i = 0 ; i < data_size; i++) {
		switch (i) {
		case 0:
			write_non_cluster_reg(block_id,
					EMEM_MC_REG_IND_DATA0, data_0);
			break;
		case 1:
			write_non_cluster_reg(block_id,
					EMEM_MC_REG_IND_DATA1, data_1);
			break;
		default:
			error("invalid indirect data size");
			break;
		}
	}

	write_non_cluster_reg(block_id, EMEM_MC_REG_IND_ADDR, address);
	write_non_cluster_reg(block_id, EMEM_MC_REG_IND_CMD, cmd);
	if (check_indirect_status(block_id) < 0)
		error("indirect register=0x%x write failed on block=0x%x",
				address, block_id);
}

static void get_ddr_parameters(void)
{
	char *env_freq;
	char *env_debug;
	char *env_type;
	char *env_package;
	char *env_phy_rd_vref;
	char *env_mem_vref;
	char *env_speed_bin;
	char *env_size;
	char *env_skip_mc;
	char *env_double_refresh;
	int  htol_wa;
	union crg_gen_purp_1 gen_purp_1;
	union crg_gen_purp_2 gen_purp_2;
	u32 boot_cfg, package = 8;

	memcpy(&current_ddr_params, &ddr_config_0, sizeof(struct ddr_params));

	env_skip_mc = getenv("ddr_skip_mc");
	if(env_skip_mc)
		skip_mc_mask = simple_strtoul(env_skip_mc, NULL, 16);
	env_size = getenv("ddr_size");
	if(env_size)
		current_ddr_params.size = simple_strtoul(env_size, NULL, 10);

	env_freq = getenv("ddr_freq");
	if(env_freq)
		current_ddr_params.clock_frequency = simple_strtoul(env_freq, NULL, 10);

	htol_wa = getenv_yesno("htol_wa");
	if(htol_wa == -1 || htol_wa == 0)
		do_htol_wa = false;

	if(current_ddr_params.clock_frequency <= 400)
		current_ddr_params.pll_freq = DDR_400_MHz;
	else if(current_ddr_params.clock_frequency <= 533)
		current_ddr_params.pll_freq = DDR_533_MHz;
	else if(current_ddr_params.clock_frequency <= 667)
		current_ddr_params.pll_freq = DDR_667_MHz;
	else if(current_ddr_params.clock_frequency <= 800)
		current_ddr_params.pll_freq = DDR_800_MHz;
	else if(current_ddr_params.clock_frequency <= 933)
		current_ddr_params.pll_freq = DDR_933_MHz;
	else if(current_ddr_params.clock_frequency <= 966)
		current_ddr_params.pll_freq = DDR_966_MHz;
	else if(current_ddr_params.clock_frequency <= 1000)
		current_ddr_params.pll_freq = DDR_1000_MHz;
	else if(current_ddr_params.clock_frequency <= 1033)
		current_ddr_params.pll_freq = DDR_1033_MHz;
	else if(current_ddr_params.clock_frequency <= 1066)
		current_ddr_params.pll_freq = DDR_1066_MHz;
	else if(current_ddr_params.clock_frequency <= 1100)
		current_ddr_params.pll_freq = DDR_1100_MHz;
	else if(current_ddr_params.clock_frequency <= 1133)
		current_ddr_params.pll_freq = DDR_1133_MHz;
	else if(current_ddr_params.clock_frequency <= 1166)
		current_ddr_params.pll_freq = DDR_1166_MHz;
	else if(current_ddr_params.clock_frequency <= 1200)
		current_ddr_params.pll_freq = DDR_1200_MHz;
	else if(current_ddr_params.clock_frequency <= 1333)
		current_ddr_params.pll_freq = DDR_1333_MHz;
	else if(current_ddr_params.clock_frequency <= 1466)
		current_ddr_params.pll_freq = DDR_1466_MHz;
	else if(current_ddr_params.clock_frequency <= 1600)
		current_ddr_params.pll_freq = DDR_1600_MHz;

	env_debug = getenv("ddr_debug");
	if(env_debug) {
		if(*env_debug == '1')
			set_debug(1);
		else
			set_debug(0);
	}

	env_package = getenv("ddr_package");
	if (env_package) {
		if (strcasecmp(env_package, "x16_TwinDie") == 0)
			package = 8;
		else if (*env_package == 'x' || *env_package == 'X')
			package = simple_strtoul((env_package + 1), NULL, 10);
	}

	env_type = getenv("ddr_type");
	if(env_type) {
		if ((strcasecmp(env_type, "DDR4") == 0)) {
			if (package == 16)
				current_ddr_params.type = DDR4_16BIT;
			else if (package == 8)
				current_ddr_params.type = DDR4_8BIT;
		} else if ((strcasecmp(env_type, "DDR3") == 0)) {
			if (package == 16)
				current_ddr_params.type = DDR3_16BIT;
			else if (package == 8)
				current_ddr_params.type = DDR3_8BIT;
		}
	}

	env_speed_bin = getenv("ddr_speed_bin");
	if (env_speed_bin) {
		if (IS_DDR4_DEVICE(current_ddr_params.type)) {
			if (strcasecmp(env_speed_bin, "1600J") == 0)
				current_ddr_params.speed_bin = DDR4_1600J;
			else if (strcasecmp(env_speed_bin, "1600K") == 0)
				current_ddr_params.speed_bin = DDR4_1600K;
			else if (strcasecmp(env_speed_bin, "1600L") == 0)
				current_ddr_params.speed_bin = DDR4_1600L;
			else if (strcasecmp(env_speed_bin, "1866L") == 0)
				current_ddr_params.speed_bin = DDR4_1866L;
			else if (strcasecmp(env_speed_bin, "1866M") == 0)
				current_ddr_params.speed_bin = DDR4_1866M;
			else if (strcasecmp(env_speed_bin, "1866N") == 0)
				current_ddr_params.speed_bin = DDR4_1866N;
			else if (strcasecmp(env_speed_bin, "2133N") == 0)
				current_ddr_params.speed_bin = DDR4_2133N;
			else if (strcasecmp(env_speed_bin, "2133P") == 0)
				current_ddr_params.speed_bin = DDR4_2133P;
			else if (strcasecmp(env_speed_bin, "2133R") == 0)
				current_ddr_params.speed_bin = DDR4_2133R;
			else if (strcasecmp(env_speed_bin, "2400P") == 0)
				current_ddr_params.speed_bin = DDR4_2400P;
			else if (strcasecmp(env_speed_bin, "2400R") == 0)
				current_ddr_params.speed_bin = DDR4_2400R;
			else if (strcasecmp(env_speed_bin, "2400T") == 0)
				current_ddr_params.speed_bin = DDR4_2400T;
			else if (strcasecmp(env_speed_bin, "2400U") == 0)
				current_ddr_params.speed_bin = DDR4_2400U;
			else if (strcasecmp(env_speed_bin, "2666T") == 0)
				current_ddr_params.speed_bin = DDR4_2666T;
			else if (strcasecmp(env_speed_bin, "2666U") == 0)
				current_ddr_params.speed_bin = DDR4_2666U;
			else if (strcasecmp(env_speed_bin, "2666V") == 0)
				current_ddr_params.speed_bin = DDR4_2666V;
			else if (strcasecmp(env_speed_bin, "2666W") == 0)
				current_ddr_params.speed_bin = DDR4_2666W;
			else
				error("Invalid DDR4 speed bin %s", env_speed_bin);
		} else {
			if (strcasecmp(env_speed_bin, "800D") == 0)
				current_ddr_params.speed_bin = DDR3_800D;
			else if (strcasecmp(env_speed_bin, "800E") == 0)
				current_ddr_params.speed_bin = DDR3_800E;
			else if (strcasecmp(env_speed_bin, "1066E") == 0)
				current_ddr_params.speed_bin = DDR3_1066E;
			else if (strcasecmp(env_speed_bin, "1066F") == 0)
				current_ddr_params.speed_bin = DDR3_1066F;
			else if (strcasecmp(env_speed_bin, "1066G") == 0)
				current_ddr_params.speed_bin = DDR3_1066G;
			else if (strcasecmp(env_speed_bin, "1333F") == 0)
				current_ddr_params.speed_bin = DDR3_1333F;
			else if (strcasecmp(env_speed_bin, "1333G") == 0)
				current_ddr_params.speed_bin = DDR3_1333G;
			else if (strcasecmp(env_speed_bin, "1333H") == 0)
				current_ddr_params.speed_bin = DDR3_1333H;
			else if (strcasecmp(env_speed_bin, "1333J") == 0)
				current_ddr_params.speed_bin = DDR3_1333J;
			else if (strcasecmp(env_speed_bin, "1600G") == 0)
				current_ddr_params.speed_bin = DDR3_1600G;
			else if (strcasecmp(env_speed_bin, "1600H") == 0)
				current_ddr_params.speed_bin = DDR3_1600H;
			else if (strcasecmp(env_speed_bin, "1600J") == 0)
				current_ddr_params.speed_bin = DDR3_1600J;
			else if (strcasecmp(env_speed_bin, "1600K") == 0)
				current_ddr_params.speed_bin = DDR3_1600K;
			else if (strcasecmp(env_speed_bin, "1866J") == 0)
				current_ddr_params.speed_bin = DDR3_1866J;
			else if (strcasecmp(env_speed_bin, "1866K") == 0)
				current_ddr_params.speed_bin = DDR3_1866K;
			else if (strcasecmp(env_speed_bin, "1866L") == 0)
				current_ddr_params.speed_bin = DDR3_1866L;
			else if (strcasecmp(env_speed_bin, "1866M") == 0)
				current_ddr_params.speed_bin = DDR3_1866M;
			else if (strcasecmp(env_speed_bin, "2133K") == 0)
				current_ddr_params.speed_bin = DDR3_2133K;
			else if (strcasecmp(env_speed_bin, "2133L") == 0)
				current_ddr_params.speed_bin = DDR3_2133L;
			else if (strcasecmp(env_speed_bin, "2133M") == 0)
				current_ddr_params.speed_bin = DDR3_2133M;
			else if (strcasecmp(env_speed_bin, "2133N") == 0)
				current_ddr_params.speed_bin = DDR3_2133N;
			else
				error("Invalid DDR3 speed bin %s", env_speed_bin);
		}
	}
	env_phy_rd_vref = getenv("phy_rd_vref");
	if(env_phy_rd_vref)
		current_ddr_params.phy_rd_vref = simple_strtoul(env_phy_rd_vref, NULL, 10);

	env_mem_vref = getenv("mem_vref");
	if(env_mem_vref)
		current_ddr_params.mem_vref =  simple_strtoul(env_mem_vref, NULL, 10);

	env_double_refresh = getenv ("ddr_double_refresh");
	if(env_double_refresh && !strcmp (env_double_refresh, "enabled")) {
		current_ddr_params.ddr_double_refresh = true;
	}

	/* deliver information to cp */
	gen_purp_1.fields.type = current_ddr_params.type;
	gen_purp_1.fields.size = current_ddr_params.size;
	gen_purp_1.fields.freq = current_ddr_params.clock_frequency;
	gen_purp_2.fields.mc_mask = 0xffffff;
	write_non_cluster_reg(CRG_BLOCK_ID, CRG_REG_GEN_PURP_1, gen_purp_1.reg);
	write_non_cluster_reg(CRG_BLOCK_ID, CRG_REG_GEN_PURP_2, gen_purp_2.reg);

	boot_cfg = read_non_cluster_reg(CRG_BLOCK_ID, CRG_REG_BOOT_CFG);
	if(GET_BIT(boot_cfg, 6))
		current_ddr_params.clk_ref = 100;

}
#ifdef CONFIG_NPS_DDR_DEBUG
int do_pup_fail_dump(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int enabled;

	enabled = (u32)simple_strtol(argv[1], NULL, 10);

	if (enabled == 1) {
		g_pup_fail_dump = true;
		printf("PUP fail print dump mode is activated\n");
	}
	else if (enabled == 0) {
		g_pup_fail_dump = false;
		printf("PUP fail print dump mode is deactivated\n");
	}
	else {
		error("do_pup_fail_dump: wrong mode parameter val %d. Should be 0 or 1.", enabled);
		return 1;
	}
	return 0;
}

int do_ddr_pause(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if ((u32)simple_strtol(argv[1], NULL, 10) == 1) {
		g_ddr_pause = true;
		printf("DDR pause activated\n");
	}
	else {
		g_ddr_pause = false;
		printf("DDR pause de-activated\n");
	}
	return 0;
}
#endif
int set_ddr_freq(void)
{
    union crg_ddr_x_pll_cntr ddr_x_pll_cntr;
    union crg_emi_rst emi_rst;
    union emem_mc_apb_ifc apb_ifc;
    union crg_emi_rst saved_emi_rst[EMEM_MC_NUM_OF_BLOCKS];
    u32 i, reg;

    if(get_debug())
	    printf("==== set_ddr_freq ====\n");

    for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END ; reg++) {
        emi_rst.reg = read_non_cluster_reg(CRG_BLOCK_ID, reg);
        emi_rst.fields.umi_cfg_rst_n = 1;
        saved_emi_rst[reg - CRG_REG_EMI_RST_START].reg = emi_rst.reg;
        emi_rst.fields.srst_n = 0;
        emi_rst.fields.umi_phy_rst_n = 0;
        emi_rst.fields.umi_ifc_rst_n = 0;
        write_non_cluster_reg(CRG_BLOCK_ID, reg, emi_rst.reg);

        udelay(1000);

        apb_ifc.reg = read_non_cluster_reg(emem_mc_block_id[reg - CRG_REG_EMI_RST_START],
                                                                        EMEM_MC_REG_APB_IFC);
        apb_ifc.fields.cont_rst = 1;
        write_non_cluster_reg(emem_mc_block_id[reg - CRG_REG_EMI_RST_START],
                                        EMEM_MC_REG_APB_IFC, apb_ifc.reg);

        write_non_cluster_reg(emem_mc_block_id[reg - CRG_REG_EMI_RST_START],
                                                        EMEM_MC_REG_PHY_CTRL, 0x0);
    }
    udelay(5);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_S_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.halt = 1;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_N_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.halt = 1;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    udelay(5);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_S_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.nreset = 0;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_N_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.nreset = 0;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    udelay(5);

    if (current_ddr_params.clk_ref == 100) {
                    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_PLLCFG_ADDR,
                                                    clk_ref_100[current_ddr_params.pll_freq]);
                    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_PLLCFG_ADDR,
                                                    clk_ref_100[current_ddr_params.pll_freq]);
    } else if (current_ddr_params.clk_ref == 50) {
                    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_PLLCFG_ADDR,
                                                    clk_ref_50[current_ddr_params.pll_freq]);
                    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_PLLCFG_ADDR,
                                                    clk_ref_50[current_ddr_params.pll_freq]);
    }

    udelay(5);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	DDR_S_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.nreset = 1;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_N_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.nreset = 1;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    udelay(5);

    for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
	    udelay(10000);
	    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
			    	    	    	    DDR_S_PLL_CNTR_ADDR);
	    if (ddr_x_pll_cntr.fields.pll_lock) {
		    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
				    	    	    	 DDR_N_PLL_CNTR_ADDR);
		    if (ddr_x_pll_cntr.fields.pll_lock || g_EZsim_mode)
			    break;
	    }
    }
    if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
                    error("set_ddr_freq: no pll lock ");
                    return -EBUSY;
    }
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_S_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.halt = 0;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_S_PLL_CNTR_ADDR,
		    	    	    	    ddr_x_pll_cntr.reg);
    udelay(1);
    ddr_x_pll_cntr.reg = read_non_cluster_reg(CRG_BLOCK_ID,
		    	    	    	    	    DDR_N_PLL_CNTR_ADDR);
    ddr_x_pll_cntr.fields.halt = 0;
    write_non_cluster_reg(CRG_BLOCK_ID, DDR_N_PLL_CNTR_ADDR,
		    	    	    	    	    ddr_x_pll_cntr.reg);
    udelay(5);
    /* in case of bypass use freq of DDR_667_MHz */
    for (reg = CRG_REG_EMI_RST_START; reg <= CRG_REG_EMI_RST_END; reg++)
        write_non_cluster_reg(CRG_BLOCK_ID, reg,
                                        saved_emi_rst[reg - CRG_REG_EMI_RST_START].reg);
    return 0;
}

static void set_ddr_double_refresh(void)
{
	union emem_mc_mc_ddr_if mc_ddr_if;
	u32 block;

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		mc_ddr_if.reg =  read_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_MC_DDR_IF);
		mc_ddr_if.fields.ref_hot_update = 1;
		mc_ddr_if.fields.ezref_hot_update = 1;
		write_non_cluster_reg(emem_mc_block_id[block],
			EMEM_MC_REG_MC_DDR_IF, mc_ddr_if.reg);
	}
}


static void sdram_init(void)
{
	u32 block, mc, mr_data_0, mr_data_1;
	union emem_mc_phy_ctrl phy_ctrl;

	if(get_debug())
		printf("=== sdram_init ===\n");

	phy_ctrl.reg = 0;
	phy_ctrl.fields.ck_en = 1;

	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_PHY_CTRL, phy_ctrl.reg);
		udelay(5);
		mr0_ddr4.fields.dll_reset = 1;
		mr0_ddr3.fields.dll_reset = 1;
		/* TODO - clear dll_reset at the end */
		for (mc = 0; mc < NUMBER_OF_MC_IN_BLOCK; mc++) {
			if (IS_DDR4_DEVICE(current_ddr_params.type)) {
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block],
						mc,GET_MRS_DATA_0(3, 0), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(6, mr6.reg), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(5, 0), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(4, 0), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(2, mr2_ddr4.reg), GET_MRS_DATA_1(0));
				mr_data_0 = (MR_CMD << MR_CMD_OFFSET) +
						(1 << 17) + mr1_ddr4.reg;
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(1, mr1_ddr4.reg), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(0, mr0_ddr4.reg), GET_MRS_DATA_1(1));
			} else {
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(2, mr2_ddr3.reg), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block],
						mc, GET_MRS_DATA_0(3, 0), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(1, mr1_ddr3.reg), GET_MRS_DATA_1(0));
				emem_mc_indirect_reg_write_mrs(
						emem_mc_block_id[block], mc,
						GET_MRS_DATA_0(0, mr0_ddr3.reg), GET_MRS_DATA_1(1));
			}
		}
		udelay(5);
		mr_data_0 =
			(ZQCL_MR_CMD << MR_CMD_OFFSET) + ZQCL_MR_DATA;
		mr_data_1 = 0x2100C;
		for (mc = 0; mc < NUMBER_OF_MC_IN_BLOCK; mc++)
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
						mc, mr_data_0, mr_data_1);
		udelay(5);
	}
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		for (mc = 0; mc < NUMBER_OF_MC_IN_BLOCK; mc++) {
			mr_data_0 = ((mr6.reg & 0xFF7F) + 0x80);
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc,
					GET_MRS_DATA_0(6, mr_data_0), GET_MRS_DATA_1(1));
			udelay(5);
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc,
					GET_MRS_DATA_0(6, mr6.reg), GET_MRS_DATA_1(1));
			udelay(5);
		}
		mr6_rank_vref[block][0].reg = mr6.reg;
		mr6_rank_vref[block][1].reg = mr6.reg;
	}
}

static void print_fail_pub_dump(u32 block_idx)
{
	bool cur_debug;
	if (!g_pup_fail_dump)
		return;

	printf("START function print_fail_pub_dump, block_idx = %d\n", block_idx);
	cur_debug = get_debug();
	set_debug(false);
	print_pub_dump(block_idx);
	set_debug(cur_debug);

	return;
}

static int dll_calibration_fail_recovery(u32 ifc)
{
	union pub_dx_x_mdlr1 dx_x_mdlr1[4];
	union pub_acmdlr1 acmdlr1;
	union pub_acmdlr0 acmdlr0;
	union pub_dx_x_mdlr0 dx_x_mdlr0[4];
	union pub_dx_x_lcdlr1 dx_x_lcdlr1[4];
	union pub_dx_x_lcdlr3 dx_x_lcdlr3[4];
	union pub_dx_x_lcdlr4 dx_x_lcdlr4[4];
	union pub_dx_x_lcdlr5 dx_x_lcdlr5[4];

	union pub_dx_x_lcdlr0 dx_x_lcdlr0[4];
	union pub_dx_x_lcdlr2 dx_x_lcdlr2[4];

	union pub_pgsr0 pgsr0;
	int index, half_ui;
	u32 res_acmdlr, res_dxmdlr[4];

	bool trial_1_19_12 = true; /* updated base for 19.12.2017 */

	if(SKIP_IFC(ifc))
		return 0;

	if( g_ddr_htol_debug_verbose >= 1 )
		printf("==== dll_calibration_fail_recovery  ( Phy interface %d ) ====\n", ifc); // ddr_htol_debug_verbose 1

	pgsr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR); /* Step 1 */

	if(pgsr0.fields.idone == 1) { /* TODO maybe replaced with dcdone bit, pending snps resolution */
		acmdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_ACMDLR0_REG_ADDR); /* Steps 2-5 */
		res_acmdlr = acmdlr0.fields.iprd;
		for(index = 0; index < 4; index++ ) {
			dx_x_mdlr0[index].reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_DX0MDLR0_REG_ADDR + (0x40 * index));
			res_dxmdlr[index] = dx_x_mdlr0[index].fields.iprd;
		}
	}
	else { /* Steps 7-10 */
		acmdlr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_ACMDLR1_REG_ADDR);
		res_acmdlr = acmdlr1.fields.mdld;
		for(index = 0; index < 4; index++ ) {
			dx_x_mdlr1[index].reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_DX0MDLR1_REG_ADDR + (0x40 * index));
			res_dxmdlr[index] = dx_x_mdlr1[index].fields.mdld;
		}
	}

	pub_read_modify_write(ifc, PUB_PGCR0_REG_ADDR, pgcr0, dltmode, 1); /* Step 11 */
	udelay(10);
	pub_read_modify_write(ifc, PUB_PGCR0_REG_ADDR, pgcr0, dltmode, 0);
	udelay(10);
	pub_read_modify_write(ifc, PUB_PGCR0_REG_ADDR, pgcr0, initfsmbyp, 1);
	udelay(10);
	pub_read_modify_write(ifc, PUB_PGCR0_REG_ADDR, pgcr0, initfsmbyp, 0);
	udelay(10);

	pgsr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR); /*Step 19 */
	if(pgsr0.fields.idone != 1) {
		error("dll_calibration_fail_recovery: idone bit not set. block %d  PGSR0:0x%08X", ifc, pgsr0.reg);
		return -EBUSY;
	}

	pub_read_modify_write(ifc, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x1);
	pub_read_modify_write(ifc, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);

	pub_dual_read_modify_write(ifc, PUB_ACMDLR0_REG_ADDR, acmdlr0, iprd, res_acmdlr, tprd, res_acmdlr); /* Step 21 */
	emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_ACLCDLR_REG_ADDR , 0);			/* Step 22 */

	for(index = 0; index < 4; index++ ) {
		dx_x_mdlr0[index].reg = 0;
		dx_x_mdlr0[index].fields.iprd = res_dxmdlr[index];
		dx_x_mdlr0[index].fields.tprd = res_dxmdlr[index];
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_DX0MDLR0_REG_ADDR + (index*0x40), dx_x_mdlr0[index].reg);
		/* register read for artificial delay */
		dx_x_mdlr0[index].reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_DX0MDLR0_REG_ADDR + (index*0x40));
		half_ui = DDR_ROUND_UP( res_dxmdlr[index], 2 );


		if( trial_1_19_12 ){
			pub_dual_read_modify_write(ifc, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);

			/* todo remove
			dx_x_lcdlr0[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR0_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr0[index].fields.wld = 0;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR0_REG_ADDR + (index*0x40), 1, dx_x_lcdlr0[index].reg, 0, 0x101);
			printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr0 = %u\n", ifc, index, dx_x_lcdlr0[index].reg );

			dx_x_lcdlr2[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR2_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr2[index].fields.dqsgd = 0;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR2_REG_ADDR + (index*0x40), 1, dx_x_lcdlr2[index].reg, 0, 0x101);
			printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr2 = %u\n", ifc, index, dx_x_lcdlr2[index].reg );
			*/

			dx_x_lcdlr1[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR1_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr1[index].fields.wdqd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR1_REG_ADDR + (index*0x40), 1, dx_x_lcdlr1[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr1 = %u\n", ifc, index, dx_x_lcdlr1[index].reg ); // ddr_htol_debug_verbose 4

			dx_x_lcdlr3[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR3_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr3[index].fields.rdqsd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR3_REG_ADDR + (index*0x40), 1, dx_x_lcdlr3[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr3 = %u\n", ifc, index, dx_x_lcdlr3[index].reg );// ddr_htol_debug_verbose 4

			dx_x_lcdlr4[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR4_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr4[index].fields.rdqsnd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR4_REG_ADDR + (index*0x40), 1, dx_x_lcdlr4[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr4 = %u\n", ifc, index, dx_x_lcdlr4[index].reg );// ddr_htol_debug_verbose 4

			dx_x_lcdlr5[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR5_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr5[index].fields.dqsgsd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR5_REG_ADDR + (index*0x40), 1, dx_x_lcdlr5[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 0 lcdlr5 = %u\n", ifc, index, dx_x_lcdlr5[index].reg );// ddr_htol_debug_verbose 4


			pub_dual_read_modify_write(ifc, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 1, rankwid, 1);

			/* todo remove
			dx_x_lcdlr0[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR0_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr0[index].fields.wld = 0;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR0_REG_ADDR + (index*0x40), 1, dx_x_lcdlr0[index].reg, 0, 0x101);
			printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr0 = %u\n", ifc, index, dx_x_lcdlr0[index].reg );

			dx_x_lcdlr2[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR2_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr2[index].fields.dqsgd = 0;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR2_REG_ADDR + (index*0x40), 1, dx_x_lcdlr2[index].reg, 0, 0x101);
			printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr2 = %u\n", ifc, index, dx_x_lcdlr2[index].reg );
			*/


			dx_x_lcdlr1[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR1_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr1[index].fields.wdqd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR1_REG_ADDR + (index*0x40), 1, dx_x_lcdlr1[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr1 = %u\n", ifc, index, dx_x_lcdlr1[index].reg );// ddr_htol_debug_verbose 4

			dx_x_lcdlr3[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR3_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr3[index].fields.rdqsd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR3_REG_ADDR + (index*0x40), 1, dx_x_lcdlr3[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr3 = %u\n", ifc, index, dx_x_lcdlr3[index].reg );// ddr_htol_debug_verbose 4

			dx_x_lcdlr4[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR4_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr4[index].fields.rdqsnd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR4_REG_ADDR + (index*0x40), 1, dx_x_lcdlr4[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr4 = %u\n", ifc, index, dx_x_lcdlr4[index].reg );// ddr_htol_debug_verbose 4

			dx_x_lcdlr5[index].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[ifc],
					PUB_DX0LCDLR5_REG_ADDR + (index*0x40), 0x100, g_EZsim_mode);
			dx_x_lcdlr5[index].fields.dqsgsd = half_ui;
			emem_mc_write_indirect_reg(emem_mc_block_id[ifc], PUB_DX0LCDLR5_REG_ADDR + (index*0x40), 1, dx_x_lcdlr5[index].reg, 0, 0x101);
			if( g_ddr_htol_debug_verbose >= 4 )
				printf( " ifc = %d byte = %d trial_1_19_12 : rankwid = 1 lcdlr5 = %u\n", ifc, index, dx_x_lcdlr5[index].reg );// ddr_htol_debug_verbose 4
		}
		else{
			dx_x_lcdlr1[index].reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_DX0LCDLR1_REG_ADDR + (index*0x40));
			dx_x_lcdlr1[index].fields.wdqd = half_ui;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_DX0LCDLR1_REG_ADDR + (index*0x40), dx_x_lcdlr1[index].reg);
			dx_x_lcdlr3[index].reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_DX0LCDLR3_REG_ADDR + (index*0x40));
			dx_x_lcdlr3[index].fields.rdqsd = half_ui;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_DX0LCDLR3_REG_ADDR + (index*0x40), dx_x_lcdlr3[index].reg);
			dx_x_lcdlr4[index].reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_DX0LCDLR4_REG_ADDR + (index*0x40));
			dx_x_lcdlr4[index].fields.rdqsnd = half_ui;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_DX0LCDLR4_REG_ADDR + (index*0x40), dx_x_lcdlr4[index].reg);
			dx_x_lcdlr5[index].reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], PUB_DX0LCDLR5_REG_ADDR + (index*0x40));
			dx_x_lcdlr5[index].fields.dqsgsd = half_ui;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], PUB_DX0LCDLR5_REG_ADDR + (index*0x40), dx_x_lcdlr5[index].reg);
		}
	}

	pub_read_modify_write(ifc, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(ifc, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0);
	udelay( 10 );

	return 0;
}


int phy_init(void)
{
	u32 block, value, i;
	union emem_mc_ind_cmd ind_cmd;
	union pub_pgsr0 pgsr0;
	union pub_zqcr zqcr;
	char  *env;
	bool  err_exit = false;
	bool  sw_calib_recovery = true, ddr_reset = true;

	ind_cmd.reg = 0;
	ind_cmd.fields.mem_id = 3;
	env = getenv( "sw_calib_recovery" );
	if( env )
		sw_calib_recovery = (getenv_yesno("sw_calib_recovery") == 1);
	env = getenv("ddr_reset");
	if( env)
		ddr_reset = (getenv_yesno("ddr_reset") == 1);

	if ( ddr_reset ) { /* a */
		udelay(10);
		write_non_cluster_reg(GLOBAL_0_BLOCK_ID, GLOBAL_0_REG_GPIO0_0_OUT, 0x1);
		write_non_cluster_reg(GLOBAL_0_BLOCK_ID, GLOBAL_0_REG_GPIO0_0_DRV, 0x1);
		write_non_cluster_reg(GLOBAL_0_BLOCK_ID, GLOBAL_0_REG_GPIO0_0_OUT, 0x0);
		udelay(10);
	}
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS ; block++) {
		if(SKIP_IFC(block))
			continue;

		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_PIR_REG_ADDR, 0x33); /* b */
		for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) { /* c */
			udelay(2);
			value = emem_mc_read_indirect_reg(
						emem_mc_block_id[block],
						EMEM_MC_IND_REG_PUB_STATUS,
						ind_cmd.reg, false);
			if (GET_BIT(value, 0))
				break;
		}
		if (i == INDIRECT_RETRY_NUM) {
			pgsr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
			printf("phy init: timeout in block %d, PGSR0 status = 0x%08X\n", block, pgsr0.reg);
			//print_fail_pub_dump(block);
		}

		pgsr0.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[block], PUB_PGSR0_REG_ADDR); /* d */
		if( (!g_EZsim_mode) && (!sw_calib_recovery) )
			if ((pgsr0.fields.idone != 1) ||
				(pgsr0.fields.pldone != 1) ||
				(pgsr0.fields.dcdone != 1) ||
				(pgsr0.fields.zcdone != 1)) {
					err_exit = true;
					error("phy init: found done bits not set in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
			}
		if (!g_EZsim_mode)
			if ((pgsr0.fields.zcerr != 0)) {
					error("phy init: found zcerr error bit set in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
					err_exit = true;
			}
		if ( (!g_EZsim_mode) && (sw_calib_recovery) )
			if((pgsr0.fields.idone != 1) ||	(pgsr0.fields.dcdone != 1)){
					printf("NOTE: phy init: idone or dcdone are not set n block %d PGSR0 status = 0x%08X\n", block, pgsr0.reg);
			}

		if (!g_EZsim_mode)
			if ((pgsr0.fields.zcerr == 1) || (pgsr0.fields.pldone != 1) || (pgsr0.fields.zcdone != 1)){
				error("phy init: pldone or zcdone are not set or zcerr set.block %d  PGSR0 status = 0x%08X", block, pgsr0.reg);
				err_exit = true;
			}

		zqcr.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], /* e */
							PUB_ZQCR_REG_ADDR);
		zqcr.fields.force_zcat_vt_updat = 1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_ZQCR_REG_ADDR, zqcr.reg);
		udelay(10);
		zqcr.fields.force_zcat_vt_updat = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_ZQCR_REG_ADDR, zqcr.reg);
		udelay(10);

		if( g_ddr_dump_verbose >= 2 ){ /* debug verbuse level 2 */
			printf( "### after phy_init done bits check: block %u:\n", block );
			print_pub_dump( block );
		}

		if ( sw_calib_recovery ) { /* f */
			if (dll_calibration_fail_recovery(block) != 0) {
				error("phy init:  dll_calibration_fail_recovery failed in block %d", block);
				err_exit = true;
			}
			if( g_ddr_dump_verbose >= 3 ){ /* debug verbuse level 3 */
				printf( "#### after ddl recovery pub_dump IF %u\n ###\n", block);
				print_pub_dump(block);
			}
		}

		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],	PUB_PIR_REG_ADDR, 0x40001); /* g */

	}

    if (err_exit) { /* TODO check */
          print_fail_pub_dump(block);
          return -EBUSY;
    }

	if ( ddr_reset ) { /* i */
		mdelay(1);
		write_non_cluster_reg(GLOBAL_0_BLOCK_ID, GLOBAL_0_REG_GPIO0_0_OUT, 0x1);
		mdelay(1);
	}
	return 0;
}

static int sw_read_dqs_normalization(u32 block)
{
	u32 byte_nom_err, bl_index, byte_dgsl, byte_dqsgd, byte_iprd/* , byte_nom_done */;
	union pub_dx_x_gtr0 dx_x_gtr0;
	union pub_dx_x_mdlr0 dx_x_mdlr0;
	union pub_dx_x_lcdlr2 dx_x_lcdlr2;

	if( g_ddr_htol_debug_verbose >= 1 )
		printf("==== sw_read_dqs_normalization  ( Phy interface %d ) ====    \n", block );// ddr_htol_debug_verbose 1

	byte_nom_err = 0;
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);

	for( bl_index=0 ; bl_index<4 ; bl_index++ ){
		/* 6 */
		byte_dgsl = 0;
		byte_dqsgd = 0;
		byte_iprd = 0;
		/* byte_nom_done = 0; */

		dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (bl_index*0x40));
		byte_dgsl = dx_x_gtr0.fields.dgsl;
		dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (bl_index*0x40));
		byte_iprd = dx_x_mdlr0.fields.iprd;
		dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (bl_index*0x40));
		byte_dqsgd = dx_x_lcdlr2.fields.dqsgd;
		if( g_ddr_htol_debug_verbose >= 2 ) {// ddr_htol_debug_verbose 2
			printf( " ifc = %d byte = %d dqs_norm : pre-value DXnGTR0 byte_dgsl %u\n", block, bl_index, byte_dgsl );
			printf( " ifc = %d byte = %d dqs_norm : pre-value DXnMDLR0 byte_iprd %u\n", block, bl_index, byte_iprd );
			printf( " ifc = %d byte = %d dqs_norm : pre-value DXnLCDLR2 byte_dqsgd %u\n", block, bl_index, byte_dqsgd );
		}

		/* 10 */
		if( byte_dgsl > 0 ){
			/* 11 */
			byte_dgsl = byte_dgsl - 1;
			byte_dqsgd = byte_dqsgd + byte_iprd;

			/* 13 */
			pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + (bl_index*0x40), dx_x_gtr0, dgsl, byte_dgsl);
			pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + (bl_index*0x40), dx_x_lcdlr2, dqsgd, byte_dqsgd);
			/* byte_nom_done = 1; */

			if( g_ddr_htol_debug_verbose >= 2 ){ // ddr_htol_debug_verbose 2
				printf( " ifc = %d byte = %d dqs_norm : post-value DXnGTR0 byte_dgsl %u\n", block, bl_index, byte_dgsl );
				printf( " ifc = %d byte = %d dqs_norm : post-value DXnLCDLR2 byte_dqsgd %u\n", block, bl_index, byte_dqsgd );
			}
		}
		else{
			/* 16 */
			byte_nom_err = 1;
			error( "ifc = %d byte = %d Step 16\n", block, bl_index );
		}
	}

	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x0);
	udelay( 10 );

	if ( byte_nom_err ) {
		error("sw_read_dqs_normalization failed emem_mc block 0x%x", emem_mc_block_id[block] );
		return -1;
	}

	return 0;
}


/* TODO written with Neil fron snps - for htol debugging */
int ddr_int_loopback_test( u32 block )
{
	u32 byte_lane = 0, i;
	union pub_bistgsr bistgsr;
	union pub_bistwer1 bistwer1;
	union pub_bistwcsr bistwcsr;
	bool ddr_debug = get_debug();

	printf( "====== ddr_int_loopback_test block %u =====\n", block );

	//set_debug(1);

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, dual_chn, 0);


	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, iolb, 1);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, lbdqss, 1);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, lbgdqs, 0);
	pub_read_modify_write(block, PUB_BISTLSR_REG_ADDR, bistlsr, seed, 0x1234abcd);
	pub_read_modify_write(block, PUB_BISTAR0_REG_ADDR, bistar0, bbank, 0);
	pub_read_modify_write(block, PUB_BISTAR0_REG_ADDR, bistar0, bcol, 0);
	pub_read_modify_write(block, PUB_BISTAR1_REG_ADDR, bistar1, bmrank, 1);
	pub_read_modify_write(block, PUB_BISTAR1_REG_ADDR, bistar1, bainc, 8);
	pub_read_modify_write(block, PUB_BISTAR1_REG_ADDR, bistar1, brank, 0);

	pub_read_modify_write(block, PUB_BISTAR2_REG_ADDR, bistar2, bmbank, 15);
	pub_read_modify_write(block, PUB_BISTAR2_REG_ADDR, bistar2, bmcol, 1016);
	pub_read_modify_write(block, PUB_BISTAR3_REG_ADDR, bistar3, brow, 0);
	pub_read_modify_write(block, PUB_BISTAR4_REG_ADDR, bistar4, bmrow, 0x0ffff);
	pub_read_modify_write(block, PUB_BISTAR4_REG_ADDR, bistar4, bmrow, 0x0ffff);

	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bmode, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdxen, 1);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdpat, 2);

	pub_read_modify_write(block, PUB_BISTUDPR_REG_ADDR, bistudpr, budp0, 0xa5a5);
	pub_read_modify_write(block, PUB_BISTUDPR_REG_ADDR, bistudpr, budp1, 0xa5a5);

	pub_read_modify_write(block, PUB_BISTWCR_REG_ADDR, bistwcr, bwcnt, 0x0020);

	pub_read_modify_write(block, PUB_BISTMSKR0_REG_ADDR, bistmskr, reserved, 0x0);
	pub_read_modify_write(block, PUB_BISTMSKR1_REG_ADDR, bistmskr, reserved, 0xF8000080);
	pub_read_modify_write(block, PUB_BISTMSKR2_REG_ADDR, bistmskr, reserved, 0x0);

	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bccsel, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bcksel, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdxsel, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdpat, 3);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdmen, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bacen, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdxen, 1);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bsonf, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, nfail, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, binf, 0);
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bmode, 0);

	/*
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, bdxsel, byte_lane); / * 5.8 * /
	udelay( 1 );
	pub_read_modify_write(block, PUB_DX0LCDLR3_REG_ADDR + ( byte_lane*0x40 ), dx_x_lcdlr3, rdqsd, 0);
	udelay( 1 );
	pub_read_modify_write(block, PUB_DX0LCDLR4_REG_ADDR + ( byte_lane*0x40 ), dx_x_lcdlr4, rdqsnd, 0);
	udelay( 1 );
	*/

	udelay( 1 );
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, binst, 2);
	mdelay( 1 );
	pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 0);
	mdelay( 1 );
	pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 1);
	mdelay( 1 );


	pub_read_modify_write(block, PUB_BISTLSR_REG_ADDR, bistlsr, seed, 0x1234abcd);
	mdelay( 1 );

	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, binst, 3);
	mdelay( 1 );
	pub_read_modify_write(block, PUB_BISTRR_REG_ADDR, bistrr, binst, 1);
	mdelay( 10 );


	for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
		udelay(1000);
		bistgsr.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[block], PUB_BISTGSR_REG_ADDR);
		if (bistgsr.fields.bdone == 1)
			break;
	}
	if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
		error("ddr_int_loopback_test: timeout in INTERNAL LOOPBACK in block %d, bistgsr status = 0x%08X", block, bistgsr.reg);
	}

	bistwer1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_BISTWER1_REG_ADDR);
	bistwcsr.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_BISTWCSR_REG_ADDR);
	printf( " ifc = %d byte = %d int_lb : BISTGSR 0x%08x\n", block, byte_lane, bistgsr.reg );
	printf( " ifc = %d byte = %d int_lb : BISTWER1 0x%08x\n", block, byte_lane, bistwer1.reg );
	printf( " ifc = %d byte = %d int_lb : BISTWCSR 0x%08x\n", block, byte_lane, bistwcsr.reg ); /* 5.13 */

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, dual_chn, 1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x0);
	udelay( 10 );

	set_debug(ddr_debug);

	return 0;
}

/* TODO consider removing - used for debug steps of htol */
static void sw_wa_inc_all_lcdlr(u32 block)
{
	u32 bl_index;
	union pub_dx_x_lcdlr0 lcdlr0;
	union pub_dx_x_lcdlr1 lcdlr1;
	union pub_dx_x_lcdlr2 lcdlr2;
	union pub_dx_x_lcdlr3 lcdlr3;
	union pub_dx_x_lcdlr4 lcdlr4;
	union pub_dx_x_lcdlr5 lcdlr5;
	bool debug_prints = false;

	printf("==== sw_wa_inc_all_lcdlr (block 0x%x)  ====    \n", block );

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);

	if( 0 ){
		pub_read_modify_write(block, PUB_PGCR4_REG_ADDR, pgcr4, dxddlld, 0x5);
	}

	for( bl_index=0;bl_index<4;bl_index++)
	{
		lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr0 %u\n", block, bl_index, lcdlr0.reg );
		lcdlr0.fields.wld++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (bl_index*0x40), lcdlr0.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr0 %u\n", block, bl_index, lcdlr0.reg );

		lcdlr1.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR1_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr1 %u\n", block, bl_index, lcdlr1.reg );
		lcdlr1.fields.wdqd++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR1_REG_ADDR + (bl_index*0x40), lcdlr1.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr1 %u\n", block, bl_index, lcdlr1.reg );

		lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr2 %u\n", block, bl_index, lcdlr2.reg );
		lcdlr2.fields.dqsgd++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (bl_index*0x40), lcdlr2.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr2 %u\n", block, bl_index, lcdlr2.reg );

		lcdlr3.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR3_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr3 %u\n", block, bl_index, lcdlr3.reg );
		lcdlr3.fields.rdqsd++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR3_REG_ADDR + (bl_index*0x40), lcdlr3.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr3 %u\n", block, bl_index, lcdlr3.reg );

		lcdlr4.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR4_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr4 %u\n", block, bl_index, lcdlr4.reg );
		lcdlr4.fields.rdqsnd++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR4_REG_ADDR + (bl_index*0x40), lcdlr4.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr4 %u\n", block, bl_index, lcdlr4.reg );

		lcdlr5.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR5_REG_ADDR + (bl_index * 0x40));
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : read lcdlr5 %u\n", block, bl_index, lcdlr5.reg );
		lcdlr5.fields.dqsgsd++;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR5_REG_ADDR + (bl_index*0x40), lcdlr5.reg);
		if( debug_prints )
			printf( " ifc = %d byte = %d inc_lcdlr : write lcdlr5 %u\n", block, bl_index, lcdlr5.reg );
	}

	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0);

	return;
}


static void seed_write_bdls( u32 block )
{
	int i;
	union pub_dx_x_bdlr0 bdlr0;
	union pub_dx_x_bdlr1 bdlr1;
	union pub_dx_x_bdlr2 bdlr2;

	bdlr0.fields.dq0wbd =
			bdlr0.fields.dq1wbd =
			bdlr0.fields.dq2wbd =
			bdlr0.fields.dq3wbd = 32;

	bdlr1.fields.dq4wbd =
			bdlr1.fields.dq5wbd =
			bdlr1.fields.dq6wbd =
			bdlr1.fields.dq7wbd = 32;

	bdlr2.fields.dmwbd =
			bdlr2.fields.dswdb =
			bdlr2.fields.dsoebd = 32;


	for( i=0 ; i <4 ; i++ ){
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[block],
				PUB_DX0BDLR0_REG_ADDR + (i*0x40),
				bdlr0.reg );

		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[block],
				PUB_DX0BDLR1_REG_ADDR + (i*0x40),
				bdlr1.reg );

		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[block],
				PUB_DX0BDLR2_REG_ADDR + (i*0x40),
				bdlr2.reg );
	}

	return;
}



static int sw_read_dqs(u32 block)
{
	union pub_dcusr0 dcusr0;
	union pub_dcusr1 dcusr1;
	union pub_dx_x_mdlr0 dx_x_mdlr0;
	union pub_dx_x_lcdlr3 lcdlr3;
	union pub_dx_x_lcdlr4 lcdlr4;
	union pub_dx_x_lcdlr5 lcdlr5;

	/* union pub_dx_x_lcdlr2 dx_x_lcdlr2 */;

	int i, j;
	u32 byte_dgsl, mc, bl_index, addr_offset, dqsg_rank;
	u32 dqsg_limit, byte_dqsgd, byte_qsgdone, byte_qsgerr;
	u32 byte_first_pass, byte_last_pass, repeat_count, dqsg_byte;
	u32 mr_data_0, mr_data_1;
	bool error_msg = false, set_qsgerr = false, dqs_fail = false;;
	bool do17 = false;
	bool inloop_dxterm = false; /* false for now, lower failure probability */
	bool trial_1_5_12 = false; /* to be considered to be official= off */
	bool trial_1_6_12 = false; /* to be considered to be official / 11.12.2017 - pass with flag at false and low voltage*/
	bool trial_1_17_12 = true;	/* official update */

	if( g_ddr_htol_debug_verbose >= 1 )
		printf("==== sw_read_dqs  ( Phy interface %d ) ====\n", block); // ddr_htol_debug_verbose 1

	for( mc = 0; mc < 2; mc++ ) {
		/* additional pre-charge all command */
		mr_data_0 = (0x2a << MR_CMD_OFFSET) + 0x400;
		mr_data_1 = 0x2100C;
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc, mr_data_0, mr_data_1);
		/* end addition */

		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(3, (mr3_ddr4.reg + 0x4)), GET_MRS_DATA_1(1));
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(4, (mr4_ddr4.reg + 0x400)), GET_MRS_DATA_1(1));
	}

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);

	if( trial_1_5_12 ){ /* trial 1 - 5.12 */
		pub_read_modify_write(block, PUB_PGCR4_REG_ADDR, pgcr4, dxddlld, 0x1f);
		pub_dual_read_modify_write(block, PUB_PGCR7_REG_ADDR, pgcr7, dxgdbyp, 1, dxddlldt, 1);
	}

	for (bl_index = 0;bl_index < 4; bl_index++) {
		addr_offset = bl_index * 0x40;
		if( trial_1_6_12 ){
			pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dqsgpdr, 1);
		}
		else {
			pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dxen, 0);
		}
		if ( !inloop_dxterm ){
			pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0x5555);
			pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dstemode, 0x1);
		}
	}
	pub_read_modify_write(block, PUB_DXCCR_REG_ADDR, dxccr, qscnten, 0x0); /* 4 */
	for( dqsg_rank = 0;dqsg_rank < 2;dqsg_rank++) {
		for( bl_index = 0;bl_index < 2;bl_index++) {
			byte_dgsl = 0;
			dqsg_limit = 0;
			byte_dqsgd = 0;
			byte_qsgdone = 0;
			byte_qsgerr = 0;
			byte_first_pass	= 0;
			byte_last_pass = 0;
			repeat_count = 8;
			set_qsgerr = false;
			error_msg = false;

			dqsg_byte = (dqsg_rank * 2) + bl_index;
			addr_offset = dqsg_byte * 0x40;
			if( trial_1_6_12 ){
				pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dqsgpdr, 0);
			}
			else {
				pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dxen, 1); /* 9 */
			}
			if ( inloop_dxterm ){
				pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0x5555);
				pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dstemode, 0x1);
			}
			dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (dqsg_byte * 0x40));
			dqsg_limit = dx_x_mdlr0.fields.iprd * 2;
			pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, dqsg_rank, rankwid, dqsg_rank);

			pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl);
			pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + addr_offset, dx_x_lcdlr2, dqsgd, byte_dqsgd);
			if( trial_1_17_12 ){
				if( g_ddr_htol_debug_verbose >= 4 )
					printf( "### start trial_1_17_12\n"); // ddr_htol_debug_verbose 4
				lcdlr3.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR3_REG_ADDR + (dqsg_byte * 0x40));
				if( g_ddr_htol_debug_verbose >= 4 )
					printf( " ifc = %d byte = %d read_dqs : read lcdlr3 %u\n", block, dqsg_byte, lcdlr3.reg );// ddr_htol_debug_verbose 4
				lcdlr3.fields.rdqsd++;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR3_REG_ADDR + (dqsg_byte*0x40), lcdlr3.reg);
				if( g_ddr_htol_debug_verbose >= 4 )
					printf( " ifc = %d byte = %d read_dqs : write lcdlr3 %u\n", block, dqsg_byte, lcdlr3.reg );// ddr_htol_debug_verbose 4

				lcdlr4.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR4_REG_ADDR + (dqsg_byte * 0x40));
				if( g_ddr_htol_debug_verbose >= 4 )
					printf( " ifc = %d byte = %d read_dqs : read lcdlr4 %u\n", block, dqsg_byte, lcdlr4.reg );// ddr_htol_debug_verbose 4
				lcdlr4.fields.rdqsnd++;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR4_REG_ADDR + (dqsg_byte*0x40), lcdlr4.reg);
				if( g_ddr_htol_debug_verbose >= 4 )
					printf( " ifc = %d byte = %d read_dqs : write lcdlr4 %u\n", block, dqsg_byte, lcdlr4.reg ); // ddr_htol_debug_verbose 4

				if( g_ddr_htol_debug_verbose >= 4 )
					printf( "### end trial_1_17_12\n"); // ddr_htol_debug_verbose 4
			}

			/* todo, check if 14.a valid */
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DCUAR_REG_ADDR, 0x400); /* 14.a */
			for(j = 0; j < 2; j++) {	
				for(i = 0; i < 5; i++)
					emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DCUDR_REG_ADDR, 0);
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DCUDR_REG_ADDR, 0x00018280 + (dqsg_rank * 0x20));
			}
			for(i = 0; i < 5; i++)
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DCUDR_REG_ADDR, 0);
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DCUDR_REG_ADDR, 0x000183C0 + (dqsg_rank * 0x20)); /* 13.i */
			pub_read_modify_write(block, PUB_DCURR_REG_ADDR, dcurr, eaddr, 0x2);
			pub_dual_read_modify_write(block, PUB_DCULR_REG_ADDR, dculr, leaddr, 0x2, lcnt, 0x7); /* 13.k */
			/* todo trail to be performed:
			 pub_read_modify_write(block, PUB_DCULR_REG_ADDR, dculr, xleaddr, 0 ); */

			for( j = 0; j < (INDIRECT_RETRY_NUM * INDIRECT_RETRY_NUM); j++) {
				if( g_ddr_htol_debug_verbose >= 3 )
					printf( " ifc = %d byte = %d step 14.l : byte_dqsgd %u\n", block, dqsg_byte,byte_dqsgd );// ddr_htol_debug_verbose 3

				pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + addr_offset, dx_x_lcdlr2, dqsgd, byte_dqsgd); /* 13.l */
				pub_read_modify_write(block, PUB_DCURR_REG_ADDR, dcurr, dinst, 0x2);
				pub_read_modify_write(block, PUB_DCURR_REG_ADDR, dcurr, dinst, 0x4);
				pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 0x0);
				pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 0x1);
				pub_read_modify_write(block, PUB_DCURR_REG_ADDR, dcurr, dinst, 0x1);
				for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
					udelay(1000);
					dcusr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DCUSR0_REG_ADDR);
					if (dcusr0.fields.rdone == 1)
						break;
				}
				if( g_ddr_htol_debug_verbose >= 3 )
					printf( " ifc = %d byte = %d step 14.r : DCUSR0.RDONE = %u\n", block, dqsg_byte, dcusr0.fields.rdone ); // ddr_htol_debug_verbose 3

				if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) { /*TODO check retries */
					error("sw_read_dqs:timeout in block %d, DCUSR0 = 0x%08X", block, dcusr0.reg);
					print_fail_pub_dump(block);
					return -EBUSY;
				}
				dcusr1.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DCUSR1_REG_ADDR); /* s */
				if( g_ddr_htol_debug_verbose >= 3 )
					printf( " ifc = %d byte = %d step 14.s : DCUSR1.RDCNT = %u\n", block, dqsg_byte, dcusr1.fields.rdcnt );// ddr_htol_debug_verbose 3
				if(!((dcusr1.fields.rdcnt == (2 * repeat_count)) && (byte_first_pass == 0) && (byte_last_pass == 0))) { /* t */
					if((dcusr1.fields.rdcnt != ( 2 * repeat_count )) && (byte_first_pass == 1) && (byte_last_pass == 0)) { /* u */
						byte_last_pass = 1; /* x */
						if( g_ddr_htol_debug_verbose >= 3 )
							printf("LAST PASS : byte %d byte_dgsl = %d byte_dqsgd = %d\n", dqsg_byte, byte_dgsl, byte_dqsgd);// ddr_htol_debug_verbose 3
						byte_qsgdone = 1;
						break;
					}
				} else {
					byte_first_pass = 1; /* w */
					if( g_ddr_htol_debug_verbose >= 3 )
						printf("FIRST PASS : byte %d byte_dgsl = %d byte_dqsgd = %d\n", dqsg_byte, byte_dgsl, byte_dqsgd);// ddr_htol_debug_verbose 3
					byte_dgsl++;
					pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl);
					byte_last_pass = 1; /* x */
					byte_qsgdone = 1;
					break;
				}
				if(((byte_first_pass == 1) && (byte_last_pass == 1)) || (byte_dqsgd < dqsg_limit)) { /* y & z */
					byte_dqsgd++; /* ee */
					continue;
				}
				if(byte_dgsl == 18) { /* aa */
					set_qsgerr = true;
					break;
				}
				byte_dgsl += 2; /* bb */
				pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl); /* cc */
				if( g_ddr_htol_debug_verbose >= 3 )
					printf( " ifc = %d byte = %d step 14.cc : byte_dgsl %u\n", block, dqsg_byte, byte_dgsl );// ddr_htol_debug_verbose 3

				byte_dqsgd = 1; /* dd + ee */
			}
			if(set_qsgerr) /* gg */
				byte_qsgerr = 1;
			/* 15 */
			if((byte_qsgdone == 0) || (byte_qsgerr == 1))
				error_msg = dqs_fail = true;
			else {
				if (byte_dgsl != 0) {
					if ( 0 /* do17, step 17 is omitted altogether from flow */ ) {
						if ( byte_dqsgd >= (dqsg_limit / 2)) {
							pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl); /* 18 */
						}
						else if ( byte_dgsl == 0 ) {
							error_msg = dqs_fail = true;
						}
						else {
							byte_dgsl--;
							byte_dqsgd += (dqsg_limit / 2);
							pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + addr_offset, dx_x_lcdlr2, dqsgd, byte_dqsgd);
							pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl); /* 18 */
							error_msg = false;
						}
					} else {
						pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + addr_offset, dx_x_gtr0, dgsl, byte_dgsl); /* 18 */
						error_msg = false;
					}
					
				} else if(byte_dqsgd > (dqsg_limit / 2)) { /*20*/
					byte_dqsgd -= (dqsg_limit/2 );
					pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + addr_offset, dx_x_lcdlr2, dqsgd, byte_dqsgd);
					error_msg = false;
				}
			}
			if(error_msg) { /* 24 */
				error(" ifc = %u  byte = %u Read DQS failure\n", block, dqsg_byte);
				error("Block %u The Maximum system latency reached without finding first pass or the Read DQS Gate position cannot be decremented by 1 UI", block);
			}
			if( trial_1_6_12 ){
				pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dqsgpdr, 1);
			}
			else {
				pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dxen, 0x0); /* 25 */
			}
			if ( inloop_dxterm ){
				pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0x0);
				pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dstemode, 0x0);
			}
		}
	}
	/* Trial 2 29.11
	dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (2 * 0x40));
	dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (2 * 0x40));
	byte_dqsgd = dx_x_lcdlr2.fields.dqsgd + ( dx_x_mdlr0.fields.iprd /4);
	pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + (2 * 0x40), dx_x_lcdlr2, dqsgd, byte_dqsgd);

	dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (3 * 0x40));
	dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (3 * 0x40));
	byte_dqsgd = dx_x_lcdlr2.fields.dqsgd + ( dx_x_mdlr0.fields.iprd /4);
	pub_read_modify_write(block, PUB_DX0LCDLR2_REG_ADDR + (3 * 0x40), dx_x_lcdlr2, dqsgd, byte_dqsgd );
	end trial */


	pub_read_modify_write(block, PUB_DXCCR_REG_ADDR, dxccr, qscnten, 1); /* step 28 */
	for( bl_index = 0;bl_index < 4;bl_index++) {
		addr_offset = bl_index * 0x40;
		if( trial_1_6_12 ){
			pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dqsgpdr, 0);
		}
		else {
			pub_read_modify_write(block, PUB_DX0GCR0_REG_ADDR + addr_offset, dx_n_gcr0, dxen, 0x1);
		}
		if ( !inloop_dxterm ){
			pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0x0);
			pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dstemode, 0x0);
		}
	}
	if( trial_1_5_12 ){ /* trial 1 - 5.12 */
		pub_read_modify_write(block, PUB_PGCR4_REG_ADDR, pgcr4, dxddlld, 0);
		pub_dual_read_modify_write(block, PUB_PGCR7_REG_ADDR, pgcr7, dxgdbyp, 0, dxddlldt, 0);
	}

	pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);

	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x0);
	udelay( 10 );

	for( mc = 0; mc < 2; mc++ ) {
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc, GET_MRS_DATA_0(4, mr4_ddr4.reg), GET_MRS_DATA_1(1));
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc, GET_MRS_DATA_0(3, mr3_ddr4.reg), GET_MRS_DATA_1(1));

		/* additional pre-charge all command */
		mr_data_0 = (0x2a << MR_CMD_OFFSET) + 0x400;
		mr_data_1 = 0x2100C;
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc, mr_data_0, mr_data_1);
		/* end addition */

	}
	if(dqs_fail){
        print_fail_pub_dump(block);
		return -EBUSY;
	}

	pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 0x0);
	udelay(1);
	pub_read_modify_write(block, PUB_PGCR0_REG_ADDR, pgcr0, phyfrst, 0x1);
	udelay(1);



	return 0;
}



static int sw_write_leveling(u32 block, u32 wl_mr1_data)
{
	union pub_dx_x_lcdlr0 dx_x_lcdlr0[4];
	union pub_dx_x_gsr0 dx_x_gsr0[4];
	union pub_dx_x_mdlr0 dx_x_mdlr0[4];
	union pub_pgsr0 pgsr0;
	union pub_dx_x_gtr0 gtr0;
	u32 wl_rank, bl_index, mc, addr_offset, mr_data_0, mr_data_1;
	u32 byte_prevdq, wl_byte, byte_wldly, byte_wldone, byte_dq;
	u32 i, byte_validated, wl_validation_window, wl_validation_count, wl_step;
	int wldly;
	bool byte_error = false;
	bool trial_3_18_12 = false;
	bool trial_1_20_12 = true; /* should be always enabled */
	bool trial_2_20_12 = true; /* should be always enabled */

	if( g_ddr_htol_debug_verbose >= 1 )
		printf("==== sw_write_leveling  ( Phy interface %d ) ====    \n", block); // ddr_htol_debug_verbose 1

	for ( bl_index = 0; bl_index < 4;bl_index++ ) {
		addr_offset = bl_index * 0x40;
		pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dsoemode, 1);
		pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0x5555);
	}

	for( mc = 0; mc < 2; mc++ ) {
		/************************/
		/* pre charge all command */
		mr_data_0 = (0x2a << MR_CMD_OFFSET) + 0x400;
		mr_data_1 = 0x2100C;
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc, mr_data_0, mr_data_1);

		/* mr0 dll reset off operation */
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
			mc,GET_MRS_DATA_0(0, (mr0_ddr4.reg & 0xfeff)), GET_MRS_DATA_1(1));

		/* refresh command operation */
		mr_data_0 = (0x32 << MR_CMD_OFFSET);
		mr_data_1 = 0x2100C;
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block], mc, mr_data_0, mr_data_1);
		udelay(1);
		/************************/
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(1, (wl_mr1_data + 0x1080)), GET_MRS_DATA_1(1));
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(1, (wl_mr1_data + 0x80)), GET_MRS_DATA_1(1));
	}

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x1);
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, wlmode, 1); /* Step 4 */

	pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);

	for (wl_rank = 0; wl_rank < 2; wl_rank++) {
		for (bl_index = 0; bl_index < 2; bl_index++) {
			wl_byte = (wl_rank*2) + bl_index;
			addr_offset = wl_byte * 0x40;

			/* Step 9 */
			byte_wldly = 0;
			byte_wldone = 0;
			byte_dq	= 0;
			byte_prevdq = 1;
			byte_validated = 0;
			wl_validation_window = 0;
			wl_validation_count = 0;
			wl_step = 8;

			/* write leveling search step 10 */
			for ( wldly = 0; wldly < 0x1FF; wldly+=wl_step) {
				dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
									PUB_DX0LCDLR0_REG_ADDR + addr_offset, 0x100, g_EZsim_mode);
				dx_x_lcdlr0[wl_byte].fields.wld = wldly;
				emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + addr_offset,
												 1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],	PUB_PIR_REG_ADDR, 0x40200);
				for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
					udelay(1000);
					pgsr0.reg = emem_mc_indirect_reg_read_synop(
									emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
					if (pgsr0.fields.wldone == 1)
						break;
				}
				if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
					error("sw_write_leveling: timeout in write leveling in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
					print_fail_pub_dump(block);
					return -EBUSY;
				}
				dx_x_gsr0[wl_byte].reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[block], PUB_DX0GSR0_REG_ADDR + (wl_byte * 0x40));
				byte_dq = dx_x_gsr0[wl_byte].fields.wldq;
				if( g_ddr_htol_debug_verbose >= 3 )
					printf( " ifc = %d byte = %d Step 10.d : wldly = %d : byte_dq = %d : byte_prevdq = %d\n", block, wl_byte, wldly, byte_dq, byte_prevdq ); // ddr_htol_debug_verbose 3
				if((byte_dq == 1) && (byte_prevdq == 0)) { /* Substep e */
					byte_wldone = 1;
					byte_wldly = wldly;
					if( g_ddr_htol_debug_verbose >= 4 )
						printf( " ifc = %d byte = %d Step 10.f : byte_wldly = %d\n", block, wl_byte, byte_wldly); // ddr_htol_debug_verbose 4
					break;
				} else
					byte_prevdq = byte_dq;
			}
			if (byte_wldone == 1) {
				/* Step 11 */
				dx_x_mdlr0[wl_byte].reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (wl_byte * 0x40));
				wl_validation_window  = DIV_ROUND_CLOSEST( 5 * dx_x_mdlr0[wl_byte].fields.iprd, 8);

				/* write leveling validation loop step 12 */
				for ( ; wldly < 0x1FF; wldly++) {
					if( g_ddr_htol_debug_verbose >= 4 )
						printf( " ifc = %d byte = %d Step 12.a : wl_dly = %d byte_wldly = %d wl_validation_count = %d byte_dq = %d\n"
											, block, wl_byte, wldly, byte_wldly, wl_validation_count, byte_dq); // ddr_htol_debug_verbose 4

					dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
								PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
					dx_x_lcdlr0[wl_byte].fields.wld = wldly;
					emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40),
												 1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);

					emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_PIR_REG_ADDR, 0x40200);

					for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
						udelay(1000);
						pgsr0.reg = emem_mc_indirect_reg_read_synop(
									emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
						if (pgsr0.fields.wldone == 1)
							break;
					}
					if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
						error("sw_write_leveling: timeout in write leveling in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
						print_fail_pub_dump(block);
						return -EBUSY;
					}
					dx_x_gsr0[wl_byte].reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[block], PUB_DX0GSR0_REG_ADDR + (wl_byte * 0x40));
					byte_dq = dx_x_gsr0[wl_byte].fields.wldq;
					if( g_ddr_htol_debug_verbose >= 3 )
						printf( " ifc = %d byte = %d Step 12.d : wldly = %d : byte_dq = %d\n", block, wl_byte, wldly, byte_dq ); // ddr_htol_debug_verbose 3
					if( byte_dq == 1 ){ /* e */
						wl_validation_count++;
						if( g_ddr_htol_debug_verbose >= 3 )
							printf( " ifc = %d byte = %d Step 12.f : wl_validation_count = %d : wl_validation_window = %d\n", block, wl_byte, wl_validation_count, wl_validation_window ); // ddr_htol_debug_verbose 3
					}

					if( wl_validation_count == wl_validation_window ) { /* g */
						byte_validated = 1; /* h */
						break;
					}

				}

				/* Step 13 */
				if ( byte_validated == 1) {

					if( trial_1_20_12 ){ /* resolving non zero value on rank 0 bytes 2-3 */
						pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);
						dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
											PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
						dx_x_lcdlr0[wl_byte].fields.wld = 0;
						emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40),
																												1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);

					}

					if( trial_3_18_12 ){ /* TODO currently false, to be removed later  */
						pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);
						dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
											PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
						dx_x_lcdlr0[wl_byte].fields.wld = byte_wldly;
						emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40),
																												1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);
						if( g_ddr_htol_debug_verbose >= 4 )
							printf( " ifc = %d byte = %d trial 3_18_12 : rankwid = 0   byte_wldly = %d\n", block, wl_byte, byte_wldly ); // ddr_htol_debug_verbose 4

						pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 1, rankwid, 1);
						dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
											PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
						dx_x_lcdlr0[wl_byte].fields.wld = byte_wldly;
						emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40),
																												1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);
						if( g_ddr_htol_debug_verbose >= 4 )
							printf( " ifc = %d byte = %d trial 3_18_12 : rankwid = 1   byte_wldly = %d\n", block, wl_byte, byte_wldly ); // ddr_htol_debug_verbose 4
					}
					else{
						/* Step 14 */
						pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, wl_rank, rankwid, wl_rank);

						/* Step 15 */
						if( trial_2_20_12 ){
							if( byte_wldly > dx_x_mdlr0[wl_byte].fields.iprd  ){
								byte_wldly -= dx_x_mdlr0[wl_byte].fields.iprd;

								gtr0.reg = emem_mc_read_indirect_reg(emem_mc_block_id[block],
													PUB_DX0GTR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
								gtr0.fields.wlsl += 1;
								emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (wl_byte * 0x40),
																														1, gtr0.reg, 0, 0x101);

							}
						}
						/* Step 16 */
						dx_x_lcdlr0[wl_byte].reg  = emem_mc_read_indirect_reg(emem_mc_block_id[block],
											PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40), 0x100, g_EZsim_mode);
						dx_x_lcdlr0[wl_byte].fields.wld = byte_wldly;
						emem_mc_write_indirect_reg(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (wl_byte * 0x40),
																												1, dx_x_lcdlr0[wl_byte].reg, 0, 0x101);
						if( g_ddr_htol_debug_verbose >= 3 )
							printf( " ifc = %d byte = %d Step 14 : byte_wldly = %d\n", block, wl_byte, byte_wldly); // ddr_htol_debug_verbose 3
					}
				}
				else {
					byte_error = true;
					error("wldq is set at %d wl_byte.  wl_validation_count = %d wldly = %d", wl_byte, wl_validation_count, wldly);
				}
			} else {
				error( " ifc = %d   byte = %d   wldly reached maximum:  ( 0x%x ) \n", block, wl_byte, wldly);
				byte_error = true;
			}
			pub_dual_read_modify_write(block, PUB_RANKIDR_REG_ADDR, rankidr, rankrid, 0, rankwid, 0);

			if( g_ddr_htol_debug_verbose >= 3 )
				printf(" ifc = %d byte = %d  end step 14 : wldly = %d byte_wldly = %d byte_wldone = %d byte_dq = %d byte_prevdq = %d byte_validated = %d wl_validation_window = %d wl_validation_count = %d\n",
						block, wl_byte, wldly, byte_wldly, byte_wldone, byte_dq, byte_prevdq, byte_validated, wl_validation_window, wl_validation_count); // ddr_htol_debug_verbose 3

			if( 0 ){ /* Vishal trial write per bits skew isssue 13.12.2017 */
				union pub_dx_x_mdlr0  mdlr0;
				union pub_dx_x_lcdlr0 lcdlr0;
				union pub_dx_x_gtr0 gtr0;

				mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + addr_offset);
				lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + addr_offset);
				gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + addr_offset);
				if( g_ddr_htol_debug_verbose >= 4 ){
					printf( " ifc = %d byte = %d write_leveling : read mdlr0.iprd %u\n", block, wl_byte, mdlr0.fields.iprd ); // ddr_htol_debug_verbose 4
					printf( " ifc = %d byte = %d write_leveling : read lcdlr0.wld %u\n", block, wl_byte, lcdlr0.fields.wld );// ddr_htol_debug_verbose 4
					printf( " ifc = %d byte = %d write_leveling : read gtr0.wlsl %u\n", block, wl_byte, gtr0.fields.wlsl );// ddr_htol_debug_verbose 4
				}

				if( /*( gtr0.fields.wlsl == 0 ) && */ ( lcdlr0.fields.wld >= (2*mdlr0.fields.iprd ) ) ){
					//gtr0.fields.wlsl += 2;
					lcdlr0.fields.wld -= (2*mdlr0.fields.iprd );
				}

				if( g_ddr_htol_debug_verbose >= 4 ){
					printf( " ifc = %d byte = %d write_leveling : write lcdlr0.wld %u\n", block, wl_byte, lcdlr0.fields.wld ); // ddr_htol_debug_verbose 4
					printf( " ifc = %d byte = %d write_leveling : write gtr0.wlsl %u\n", block, wl_byte, gtr0.fields.wlsl );// ddr_htol_debug_verbose 4
				}
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + addr_offset, lcdlr0.reg);
				//emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + addr_offset, gtr0.reg);
			}
		}
	}

	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, wlmode, 0);

	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);
	pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, pub_mode, 0x0);
	udelay( 10 );
	
	for( mc = 0; mc < 2; mc++ ) {
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(1, (wl_mr1_data + 0x1080)), GET_MRS_DATA_1(1));
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(1, (wl_mr1_data + 0x1000)), GET_MRS_DATA_1(1));
		emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
				mc,GET_MRS_DATA_0(1, wl_mr1_data), GET_MRS_DATA_1(1));
	}
	for ( bl_index = 0; bl_index < 4; bl_index++ ) {
		addr_offset = bl_index * 0x40;
		pub_read_modify_write(block, PUB_DX0GCR3_REG_ADDR + addr_offset, dx_n_gcr3, dsoemode, 0);
		pub_read_modify_write(block, PUB_DX0GCR2_REG_ADDR + addr_offset, dx_n_gcr2, dxtemode, 0);
	}

	if ( byte_error ) {
		error(" At lease one byte in block %d reached the maximum value of wldly", block);
		return -1;
	}
	return 0;
}

static int sw_write_leveling_normalization( u32 block )
{
	u32 byte_nom_err, bl_index, byte_wlsl, byte_wld, byte_iprd/* , byte_nom_done */;
	union pub_dx_x_gtr0 dx_x_gtr0;
	union pub_dx_x_mdlr0 dx_x_mdlr0;
	union pub_dx_x_lcdlr0 dx_x_lcdlr0;

	if( g_ddr_htol_debug_verbose >= 1 )
		printf("==== sw_write_leveling_normalization  ( Phy interface %d ) ====    \n", block );// ddr_htol_debug_verbose 1

	byte_nom_err = 0;
	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 1);
	for( bl_index=0 ; bl_index<4 ; bl_index++ ){
		/* 6 */
		byte_wlsl = 0;
		byte_wld = 0;
		byte_iprd = 0;
		/* byte_nom_done = 0; */
		dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (bl_index*0x40));
		byte_wlsl = dx_x_gtr0.fields.wlsl;
		dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (bl_index*0x40));
		byte_iprd = dx_x_mdlr0.fields.iprd;
		dx_x_lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (bl_index*0x40));
		byte_wld = dx_x_lcdlr0.fields.wld;

		/* 10 */
		if( ( byte_wlsl == 0          ) &&
			( byte_wld  >  byte_iprd  )    ){
			/* 10 */
			continue;
		}
		else if ( byte_wlsl  > 0 ){
			/* 12 */
			byte_wlsl = byte_wlsl - 1;
			byte_wld = byte_wld + byte_iprd;
			pub_read_modify_write(block, PUB_DX0GTR0_REG_ADDR + (bl_index*0x40), dx_x_gtr0, wlsl, byte_wlsl);
			pub_read_modify_write(block, PUB_DX0LCDLR0_REG_ADDR + (bl_index*0x40), dx_x_lcdlr0, wld, byte_wld);
			/* byte_nom_done  = 1; */
		}
		else{
			/* 17 */
			byte_nom_err = 1;
			if( g_ddr_htol_debug_verbose >= 1 )
				printf( "ERROR ifc = %d byte = %d Step 17\n", block, bl_index );// ddr_htol_debug_verbose 1
		}

	}

	pub_read_modify_write(block, PUB_PGCR6_REG_ADDR, pgcr6, inhvt, 0);

	if ( byte_nom_err ) {
		error("sw_write_leveling_normalization failed emem_mc block 0x%x", emem_mc_block_id[block] );
		return -1;
	}

	return 0;
}

u32	write_bdl_seeding( u32 block )
{
	u32	seed_val = 8, i, address, reg_val;

	for( i=0;i<4;i++ ){
		address = i*0x40;
		reg_val = (seed_val<<16) | (seed_val<<8 ) | seed_val;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0BDLR2_REG_ADDR + address, reg_val);

		reg_val |= (seed_val<<24);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0BDLR0_REG_ADDR + address, reg_val);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_DX0BDLR1_REG_ADDR + address, reg_val);

		printf( "### seed_val 0x%x reg_val 0x%x\n", seed_val, reg_val );
	}

	/* not to use pub_dump
	printf( "### pub_dump after: write_bdl_seeding block %u \n", block );
	print_pub_dump(block); */


	return 0;
}



static int synop_pub_pir_step_op( u32 block, u32 cmd )
{
	u32 op, i;
	u32 pir_cmd = cmd & 0xffff00ff;
	union pub_pgsr0 pgsr0;

	for( op=0 ; op<5 ; op++ ){
		if( !( ( 1<<(11+op) ) & cmd ) )
		{
			continue;
		}

		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
										       PUB_PIR_REG_ADDR,
											   ( pir_cmd | ( 1<<(11+op) ) ));

		for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
			udelay(100);
			pgsr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block],
														PUB_PGSR0_REG_ADDR);
			if (pgsr0.fields.idone == 1)
					break;
		}
		if (i == INDIRECT_RETRY_NUM) {
			error("synop_pub_pir_step_op: data training timeout (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
					current_ddr_params.clock_frequency, block, pgsr0.reg);
			return -EBUSY;
		}

		switch( op ){
		case 0:
			if( pgsr0.fields.wladone != 1 ){
				error("synop_pub_pir_step_op: data training wladone done bit not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			else if( pgsr0.fields.wlaerr != 0 ){
				error("synop_pub_pir_step_op: data training found wlaerr error bit set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			break;
		case 1:
			if( pgsr0.fields.rddone != 1 ){
				error("synop_pub_pir_step_op: data training rddone done bit not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			else if( pgsr0.fields.rderr != 0 ){
				error("synop_pub_pir_step_op: data training found rderr error bit set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			break;
		case 2:
			if( pgsr0.fields.wddone != 1 ){
				error("synop_pub_pir_step_op: data training wddone done bit not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			else if( pgsr0.fields.wderr != 0 ){
				error("synop_pub_pir_step_op: data training found wderr error bit set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			break;
		case 3:
			if( pgsr0.fields.redone != 1 ){
				error("synop_pub_pir_step_op: data training redone done bit not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			else if( pgsr0.fields.reerr != 0 ){
				error("synop_pub_pir_step_op: data training found reerr error bit set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			break;
		case 4:
			if( pgsr0.fields.wedone != 1 ){
				error("synop_pub_pir_step_op: data training wedone done bit not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			else if( pgsr0.fields.weerr != 0 ){
				error("synop_pub_pir_step_op: data training found weerr error bit set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
				return -EBUSY;
			}
			break;
		default:
			break;

		}

	}

	return 0;
}



int	ddr_training(void)
{
	u32 wl_mr1_data, wl_mr2_data, block, mc, i;
	u32 failed_block, iter;
	u32 wl_mr1_rtt_nom_set, wl_mr2_rtt_wr_mask;
	union pub_pgsr0 pgsr0;
	union	pub_dx_x_lcdlr0 dx_x_lcdlr0;
	union	pub_dx_x_gtr0 dx_x_gtr0;
	union	pub_dx_x_lcdlr2 dx_x_lcdlr2;
	union	pub_dx_x_mdlr0 dx_x_mdlr0;
	char *env;
	bool status = true, sw_status;
	bool sw_rdqs = true, sw_rdqs_nrml = true, sw_wl = true, sw_wl_nrml = true;
	bool enable_seeding = false;
	bool pir_steps = false;
	bool trial_4_18_12 = false;


	if (get_debug())
		printf("==== ddr_training ====\n");

	wl_mr1_rtt_nom_set = IS_DDR4_DEVICE(current_ddr_params.type) ?
				0x0600:0x0004;
	wl_mr2_rtt_wr_mask = IS_DDR4_DEVICE(current_ddr_params.type) ?
				0xF1FF:0xF9FF;
	wl_mr1_data = (mr1_ddr4.reg | wl_mr1_rtt_nom_set);
	wl_mr2_data = (mr2_ddr4.reg & wl_mr2_rtt_wr_mask);

	env = getenv( "sw_write_leveling" );
	if( env )
		sw_wl = (getenv_yesno("sw_write_leveling") == 1);
	env = getenv( "sw_write_leveling_nrml" );
	if( env )
		sw_wl_nrml = (getenv_yesno("sw_write_leveling_nrml") == 1);
	env = getenv( "sw_rdqs" );
	if( env )
		sw_rdqs = (getenv_yesno("sw_rdqs") == 1);
	env = getenv( "sw_rdqs_nrml" );
	if( env )
		sw_rdqs_nrml = (getenv_yesno("sw_rdqs_nrml") == 1);


	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		if(SKIP_IFC(block))
			continue;

		if( g_ddr_dump_verbose >= 4 ){ // verbose level 4
			printf( "### ddr_training start. block %u :\n", block );
			print_pub_dump( block );
		}

		for (mc = 0; mc < NUMBER_OF_MC_IN_BLOCK; mc++) {
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
						mc, GET_MRS_DATA_0(1, wl_mr1_data), GET_MRS_DATA_1(1));
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
						mc, GET_MRS_DATA_0(2, wl_mr2_data), GET_MRS_DATA_1(1));
		}
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_MR1_REG_ADDR, wl_mr1_data);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_MR2_REG_ADDR, wl_mr2_data);
		if (sw_wl) {
			sw_write_leveling(block, wl_mr1_data);
	/*		if(sw_write_leveling(block, wl_mr1_data) != 0) {
				error("ddr_training: sw_write_leveling failed ... running again");
				sw_write_leveling(block, wl_mr1_data);
			}*/
			if( g_ddr_dump_verbose >= 3  ){ // #2 vb level 3
				printf( "### after sw_write_leveling:\n" );
				print_pub_dump( block );
			}

		}
		else {
			pub_read_modify_write(block, PUB_PGCR1_REG_ADDR, pgcr1, wlstep, 1);
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
							PUB_PIR_REG_ADDR, 0x40201);
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(1000);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
				if ((pgsr0.fields.idone == 1) &&
					(pgsr0.fields.wldone == 1))
						break;
			}
			if( g_ddr_dump_verbose >= 3 ){ // vb level 3
				printf( "### after HW_write_leveling:\n" );
				print_pub_dump( block );
			}

			if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
				error("ddr_training: timeout in write leveling in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
				status = false;
				print_fail_pub_dump(block);
				return -EBUSY;
			} else {
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
				if (pgsr0.fields.wlerr != 0) {
					error("ddr_training: write leveling failed in block %d, PGSR0 status = 0x%08X", block, pgsr0.reg);
					status = false;
					print_fail_pub_dump(block);
					return -EBUSY;
				}
			}
		}
		if( g_ddr_htol_debug_verbose >= 2 ){
			printf("\nWrite leveling results for PHY interface %d\n\n", block); // ddr_htol_debug_verbose 2
			for ( i = 0; i < 4; i++) {
				dx_x_lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (i*0x40));
				printf("dx_%d_lcdlr0 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr0.reg, block);
				dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (i*0x40));
				printf("dx_%d_lcdlr2 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr2.reg, block);
				dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (i*0x40));
				printf("dx_%d_gtr0 = 0x%x in PHY interface %d\n", i, dx_x_gtr0.reg, block);
				dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (i*0x40));
				printf("dx_%d_mdlr0 = 0x%x in PHY interface %d\n", i, dx_x_mdlr0.reg, block);
			}
		}
		for (mc = 0; mc < NUMBER_OF_MC_IN_BLOCK; mc++) {
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
						mc, GET_MRS_DATA_0(1, mr1_ddr4.reg), GET_MRS_DATA_1(1));
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[block],
						mc, GET_MRS_DATA_0(2, mr2_ddr4.reg), GET_MRS_DATA_1(1));
		}
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_MR1_REG_ADDR, mr1_ddr4.reg);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
						PUB_MR2_REG_ADDR, mr2_ddr4.reg);
	}
	for ( iter = 0; iter < 2; iter++) {
		for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
			if(SKIP_IFC(block))
				continue;

			if (sw_rdqs) {
				sw_status = sw_read_dqs(block);

				if( 0 ){
					seed_write_bdls(block);
				}

				if( sw_rdqs_nrml ){
					sw_read_dqs_normalization( block );
				}

				//sw_wa_inc_all_lcdlr( block ); -  old trial not required anymore.

				if( trial_4_18_12){
					printf( "### trial 4 18_12 DXCCR.RKLOOP = 0\n" ); /* to be removed */
					pub_read_modify_write(block, PUB_DXCCR_REG_ADDR, dxccr, rkloop, 0x0);
				}

				if( g_ddr_dump_verbose >= 2  ){ // #1 vb level 2
					printf( "### after sw_read_dqs block %u:\n", block );
					print_pub_dump( block );
				}

				if( g_ddr_htol_debug_verbose >= 2 ){
					printf("\nRead DQS results for PHY interface %d\n\n", block); // ddr_htol_debug_verbose 2
			
					for ( i=0; i<4; i++ ) {
						dx_x_lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr0 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr0.reg, block);
						dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr2 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr2.reg, block);
						dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (i*0x40));
						printf("dx_%d_gtr0 = 0x%x in PHY interface %d\n", i, dx_x_gtr0.reg, block);
						dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_mdlr0 = 0x%x in PHY interface %d\n", i, dx_x_mdlr0.reg, block);
					}
				}

				if( sw_status != 0) {
					error("ddr_training: sw_read_dqs failed");
					print_fail_pub_dump(block);
					return -EBUSY;
				}
			}
			if (DDR_TRAINING_CLI_MODE)
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
								PUB_PIR_REG_ADDR, g_ddr_training_pir_val);
			else if(sw_rdqs){
				if( enable_seeding ){
					write_bdl_seeding( block );
				}

				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block],
								PUB_PIR_REG_ADDR, 0x4F801);
			}
			else {
				if ( iter == 0 ) {
					pub_dual_read_modify_write( block, PUB_DTCR0_REG_ADDR, dtcr0, dtcmpd, 0x0, rfshdt, 0x0);
					pub_read_modify_write( block, PUB_DTCR0_REG_ADDR, dtcr0, dtmpr, 0x1);
					pub_dual_read_modify_write( block, PUB_DTCR1_REG_ADDR, dtcr1, bsten, 0x1, rdlvlen, 0x0);
					pub_read_modify_write( block, PUB_DTCR1_REG_ADDR, dtcr1, rdprmvl_trn, 0x1);
					emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_PIR_REG_ADDR, 0x40401);
				} else{
					if( enable_seeding ){
						write_bdl_seeding( block );
					}
					emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_PIR_REG_ADDR, 0x4F801);
				}
			}
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(100);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
				if (pgsr0.fields.idone == 1)
						break;
			}
			if( g_ddr_dump_verbose >= 2 ){//bv level 2
				if( iter == 0 && !sw_rdqs){
					printf( "### after HW first read DQS:\n" );
					print_pub_dump( block );
				}
			}
			if( g_ddr_dump_verbose >= 1 ){ //bv level 1
				if ( iter == 1 ){
					printf( "### after HW WLA, RDSKEW, WRSKEW, RDEYE, WREYE:\n" );
					print_pub_dump( block );
				}
				else if ( sw_rdqs ){
					printf( "### block %u after SW WL, RDQSG & HW WLA, RDSKEW, WRSKEW, RDEYE, WREYE:\n", block );
					print_pub_dump( block );
				}
			}

			if (!g_EZsim_mode) {
				if (i == INDIRECT_RETRY_NUM) {
					error("ddr_training: full data training timeout (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
					status = false;
					print_fail_pub_dump(block);
					return -EBUSY;
				} else {
					pgsr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
					if( ( !sw_rdqs ) &&
						( (pgsr0.fields.qsgdone != 1 && (iter == 0) ) ||
						  (pgsr0.fields.wladone != 1 && (iter == 1) ) ||
						  (pgsr0.fields.rddone  != 1 && (iter == 1) ) ||
						  (pgsr0.fields.wddone  != 1 && (iter == 1) ) ||
						  (pgsr0.fields.redone  != 1 && (iter == 1) ) ||
						  (pgsr0.fields.wedone  != 1 && (iter == 1) )    ) ){
							error("ddr_training: full data training found done bits not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
							failed_block = block;
							status = false;
					}
					else if( ( sw_rdqs ) &&
							 ( (pgsr0.fields.wladone != 1  && (iter == 0)) ||
							   (pgsr0.fields.rddone  != 1  && (iter == 0)) ||  /* trial 3 13.12 */
							   (pgsr0.fields.wddone  != 1  && (iter == 0)) ||  /* trial 3 13.12 */
							   (pgsr0.fields.redone  != 1  && (iter == 0)) ||
							   (pgsr0.fields.wedone  != 1  && (iter == 0))    ) ){
						error("ddr_training: full data training found done bits not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
						current_ddr_params.clock_frequency, block, pgsr0.reg);
						failed_block = block;
						status = false;
					}
					else if (get_debug()){
							printf("ddr_training: full data training done (freq %dMHz) in block %d, PGSR0 status = 0x%08X\n",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
					}

					if( ( !sw_rdqs ) &&
						( (pgsr0.fields.qsgerr != 0 && (iter == 0)) ||
						  (pgsr0.fields.wlaerr != 0 && (iter == 1)) ||
						  (pgsr0.fields.rderr  != 0 && (iter == 1)) ||
						  (pgsr0.fields.wderr  != 0 && (iter == 1)) ||
						  (pgsr0.fields.reerr  != 0 && (iter == 1)) ||
						  (pgsr0.fields.weerr  != 0 && (iter == 1))   ) ) { /* HW training only */
							error("ddr_training: full data training found error bits set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
							failed_block = block;
							status = false;

					}
					else if( ( sw_rdqs ) &&
							( (pgsr0.fields.wlaerr != 0 && (iter == 0)) ||
							  (pgsr0.fields.rderr  != 0 && (iter == 0)) ||
							  (pgsr0.fields.wderr  != 0 && (iter == 0)) ||
							  (pgsr0.fields.reerr  != 0 && (iter == 0)) ||
							  (pgsr0.fields.weerr  != 0 && (iter == 0))   ) ) { /* SW training only */
								error("ddr_training: full data training found error bits set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
								current_ddr_params.clock_frequency, block, pgsr0.reg);
								failed_block = block;
								status = false;

					}
					else{
						if (get_debug()){
							printf("ddr_training: full data training passed (freq %dMHz) in block %d, PGSR0 status = 0x%08X\n",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
						}
					}
				}
			}

			if( g_ddr_htol_debug_verbose >= 2 ){
				if ( iter == 0 && !sw_rdqs) // ddr_htol_debug_verbose 2
					printf("\nRead first DQS results for PHY interface %d\n\n", block);
				else
					printf("\nPost training results for PHY interface %d\n\n", block);
				for ( i=0; i<4; i++ ) {
						dx_x_lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr0 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr0.reg, block);
						dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr2 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr2.reg, block);
						dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (i*0x40));
						printf("dx_%d_gtr0 = 0x%x in PHY interface %d\n", i, dx_x_gtr0.reg, block);
						dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_mdlr0 = 0x%x in PHY interface %d\n", i, dx_x_mdlr0.reg, block);
				}
			}
			if ( iter == 0 && !sw_rdqs) {
				pub_read_modify_write( block, PUB_DTCR1_REG_ADDR, dtcr1, bsten, 0x0);
				pub_read_modify_write( block, PUB_DTCR1_REG_ADDR, dtcr1, rdlvlen , 0x1);
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[block], PUB_PIR_REG_ADDR, 0x40401);
				for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
					udelay(100);
					pgsr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
					if (pgsr0.fields.idone == 1)
							break;
				}
				if( g_ddr_dump_verbose >= 2 ){ //bv level 2
					printf( "### after HW second read DQS:\n" );
					print_pub_dump( block );
				}
				if (i == INDIRECT_RETRY_NUM) {
					error("ddr_training: full data training timeout (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
							current_ddr_params.clock_frequency, block, pgsr0.reg);
					status = false;
					print_fail_pub_dump(block);
					return -EBUSY;
				}
				pgsr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_PGSR0_REG_ADDR);
				if (pgsr0.fields.qsgdone != 1 ) {
					error("ddr_training: full data training found done bits not set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
										current_ddr_params.clock_frequency, block, pgsr0.reg);
					failed_block = block;
					status = false;
				}
				if (pgsr0.fields.qsgerr != 0) {
					error("ddr_training: full data training found error bits set (freq %dMHz) in block %d, PGSR0 status = 0x%08X",
											current_ddr_params.clock_frequency, block, pgsr0.reg);
					failed_block = block;
					status = false;
				}
				if( g_ddr_htol_debug_verbose >= 2 ){
					printf("\nRead second DQS results for PHY interface %d\n\n", block); // ddr_htol_debug_verbose 2
					for ( i=0; i<4; i++ ) {
						dx_x_lcdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr0 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr0.reg, block);
						dx_x_lcdlr2.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0LCDLR2_REG_ADDR + (i*0x40));
						printf("dx_%d_lcdlr2 = 0x%x in PHY interface %d\n", i, dx_x_lcdlr2.reg, block);
						dx_x_gtr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0GTR0_REG_ADDR + (i*0x40));
						printf("dx_%d_gtr0 = 0x%x in PHY interface %d\n", i, dx_x_gtr0.reg, block);
						dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[block], PUB_DX0MDLR0_REG_ADDR + (i*0x40));
						printf("dx_%d_mdlr0 = 0x%x in PHY interface %d\n", i, dx_x_mdlr0.reg, block);
					}
				}
			}

		}
		if ( sw_rdqs ) {
			break;
		}
	}

	if (!status) {
		//print_fail_pub_dump(failed_block);
		return -1;
	}

	return 0;
}

static void enable_ddr(void)
{
	union emem_mc_mc_ddr_if mc_ddr_if;
	u32 block;

	if (get_debug())
		printf("=== enable_ddr ===\n");

	for (block = 0; block < EMEM_MI_NUM_OF_BLOCKS; block++)
		write_non_cluster_reg(emem_mi_block_id[block],
					EMEM_MI_REG_CACHE_MI_IF, 0x33333);
	for (block = 0; block < EMEM_MC_NUM_OF_BLOCKS; block++) {
		mc_ddr_if.reg =  read_non_cluster_reg(emem_mc_block_id[block],
						EMEM_MC_REG_MC_DDR_IF);
		mc_ddr_if.fields.mc_en = 0x3;
		mc_ddr_if.fields.calib_en = 1;
		mc_ddr_if.fields.ezref_en = 0x0;
		mc_ddr_if.fields.ref_en = 0x3;
		write_non_cluster_reg(emem_mc_block_id[block],
					EMEM_MC_REG_MC_DDR_IF, mc_ddr_if.reg);
	}
}

static int phy_ck_setup(void)
{
	bool update = false;
	u32 tck_index, ifc, tap_delay;
	union pub_acmdlr0 acmdlr0;
	union pub_acbdlr0 acbdlr0;

	if(get_debug())
		printf("=== phy_ck_setup ===\n");
	tck_index = current_ddr_params.pll_freq;
	for (ifc = 0; ifc < EMEM_MC_NUM_OF_BLOCKS; ifc++) {
		if(SKIP_IFC(ifc))
			continue;

		if (current_ddr_params.clk[ifc].ck_0 < 15 &&
				current_ddr_params.clk[ifc].ck_1 < 15)
			continue;
		acmdlr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_ACMDLR0_REG_ADDR);
		if (acmdlr0.fields.iprd == 0) {
			printf("phy_ck_setup: IPRD fields is 0\n");
			return -1;
		}
		if (g_EZsim_mode)
			acmdlr0.fields.iprd += ifc;
		tap_delay = tCK2[tck_index] / (2 * acmdlr0.fields.iprd);
		acbdlr0.reg = 0;
		if (current_ddr_params.clk[ifc].ck_0 >= 15) {
			acbdlr0.fields.ck0bd = DIV_ROUND_CLOSEST(
				(current_ddr_params.clk[ifc].ck_0 * 1000), tap_delay);
			update = true;
		}
		if (current_ddr_params.clk[ifc].ck_1 >= 15) {
			acbdlr0.fields.ck1bd = DIV_ROUND_CLOSEST(
				(current_ddr_params.clk[ifc].ck_1 * 1000), tap_delay);
			update = true;
		}
		if (update)
			emem_mc_indirect_reg_write_synop_data0(
					emem_mc_block_id[ifc],
					PUB_ACBDLR0_REG_ADDR, acbdlr0.reg);
	}
	return 0;
}

int get_vref_index(const u32 *table, u32 size, u32 input)
{
	u32 vref_prec, vref_idx;

	vref_prec = input;/*(input * 10000) / MEM_VREF_MAX_VAL;*/
	for (vref_idx = 0; vref_idx < size; vref_idx++)
		if (vref_prec < table[vref_idx])
			break;
	if ((vref_prec - table[vref_idx - 1]) <
				(table[vref_idx] - vref_prec))
		vref_idx--;

	return vref_idx;
}

static int vref_training(void)
{
	union pub_dtcr0 dtcr0;
	union pub_bistrr bistrr;
	union pub_bistar1 bistar1;
	union pub_bistar2 bistar2;
	union pub_vtcr0 vtcr0;
	union pub_vtcr1 vtcr1;
	union pub_pgsr0 pgsr0;
	union pub_dx_x_gcr6 dx_x_gcr6;
	union pub_vtdr vtdr;
	union pub_dx_n_gcr0 dx_n_gcr0;
	u32 i, ifc, sdram_vref_min_index = 0;
	u32 sdram_vref_max_index = 0xffffffff;
	u32 sdram_min_vref, sdram_max_vref, byte, sdram_vref_range, sdram_vref_index;
	u32 range2_margin, range1_margin, sdram_r2_vref_index, sdram_r1_vref_index, sdram_vref;
	int status = 0;
	u32 saved_mr6;
	u32 mem_vref_init = (current_ddr_params.mem_vref * 10000) / MEM_VREF_MAX_VAL;
	bool invalid_vref_index = false;
#ifdef VREF_PER_RANK
	u32 byte_num, rank_num, vref_dq_val;
#else
	u32 gcr5, reg, mc;
#endif
	if(get_debug())
		printf(" === vref_training ===\n");

	saved_mr6 = mr6.reg;

	for (ifc = 0; ifc < EMEM_MC_NUM_OF_BLOCKS; ifc++) {
		if(SKIP_IFC(ifc))
			continue;
		mr6.reg = saved_mr6;
		dtcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
		dtcr0.fields.rfshdt = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_DTCR0_REG_ADDR, dtcr0.reg);

		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bdpat = 0x3;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTUDPR_REG_ADDR, 0xA5A5A5A5);

		bistar1.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);
		bistar1.fields.bainc = 0x8;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTAR1_REG_ADDR, bistar1.reg);
		bistar2.reg = 0;
		bistar2.fields.bmbank =
				GET_NUMBER_OF_BANKS(current_ddr_params.type) - 1;
		bistar2.fields.bmcol = 0x3FF;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTAR2_REG_ADDR, bistar2.reg);
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
				PUB_BISTAR4_REG_ADDR, get_max_row_num());

		vtcr1.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
		vtcr1.fields.e_num = 0x1;
		vtcr1.fields.tvrefio = 0x4;
		vtcr1.fields.vwcr = 0xf;
		vtcr1.fields.hvss = 0x1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);
		/* b */

		vtcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_VTCR0_REG_ADDR);
		vtcr0.fields.dven = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_VTCR0_REG_ADDR, vtcr0.reg);
#if 0
		vtcr1.fields.shren = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);
#endif
		/* b.2 */
#ifdef VREF_PER_BYTE
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_PIR_REG_ADDR, 0x00060001);
		for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
			udelay(1000);
			pgsr0.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR);
			if ((pgsr0.fields.idone == 1) &&
				(pgsr0.fields.vdone == 1))
				break;
		}
		if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
			error("vref_training: timeout in host vref training in block %d, PGSR0 status = 0x%08X",
									ifc, pgsr0.reg);
			print_fail_pub_dump(ifc);
			status = -EBUSY;
			continue;
		}

		if (!g_EZsim_mode)
			if (pgsr0.fields.verr) {
				error("vref_training: host vref training failed in block %d, PGSR0 status = 0x%08X",
										ifc, pgsr0.reg);
				print_fail_pub_dump(ifc);
				status = -EBUSY;
				continue;
			}
#endif
#ifdef VREF_PER_RANK
		/* per rank vref training operation */
		for(byte_num = 0; byte_num < 4; byte_num++) {
			dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
			dx_n_gcr0.fields.dxen = 0;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
							dx_n_gcr0.reg);
		}

		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bdxsel = 0xf;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		for(rank_num = 0; rank_num < 2; rank_num++) {
			vtcr1.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
			vtcr1.fields.shrnk = rank_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_VTCR1_REG_ADDR, vtcr1.reg);

			bistar1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);
			bistar1.fields.brank = rank_num;
			bistar1.fields.bmrank = rank_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_BISTAR1_REG_ADDR, bistar1.reg);

			for(byte_num = 2*rank_num; byte_num < 2*rank_num + 2; byte_num++) {
				dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc],
						PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
				dx_n_gcr0.fields.dxen = 1;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
						dx_n_gcr0.reg);
			}

			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_PIR_REG_ADDR, 0x00060001);
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(1000);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
								emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR);
				if ((pgsr0.fields.idone == 1) &&
					(pgsr0.fields.vdone == 1))
					break;
			}
			if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
				error("vref_training: timeout in host vref training in rank %d block %d, PGSR0 status = 0x%08X",
										rank_num, ifc, pgsr0.reg);
				print_fail_pub_dump(ifc);
				status = -EBUSY;
				continue;
			}

			if (!g_EZsim_mode)
				if (pgsr0.fields.verr) {
					error("vref_training: host vref training failed in rank %d  block %d, PGSR0 status = 0x%08X",
											rank_num, ifc, pgsr0.reg);
					print_fail_pub_dump(ifc);
					status = -EBUSY;
					continue;
				}

			for (byte_num = 2*rank_num; byte_num < 2*rank_num + 2; byte_num++) {
				/* b.13 */
				dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
				dx_n_gcr0.fields.dxen = 0;
				emem_mc_indirect_reg_write_synop_data0(
					emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
					dx_n_gcr0.reg);
				/* b.14 */
				res_dx_n_gcr5[byte_num].reg =
					emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc],
					PUB_DX0GCR5_REG_ADDR +
							(byte_num * 0x40));
				if(get_debug())
					printf("Device %d, byte %d, gcr5 = 0x%x\n",
						ifc,
						byte_num,
						res_dx_n_gcr5[byte_num].reg);

				/* b.15 */
				dtcr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
				dtcr0.fields.dtdbs = byte_num;
				/*dtcr0.fields.dtdbs = byte_num - (2*rank_num);
				dtcr0.fields.dtdrs = rank_num;*/
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_DTCR0_REG_ADDR, dtcr0.reg);
				/* b.16 */
				res_per_byte_vtdr[byte_num].reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTDR_REG_ADDR);
				if(get_debug())
					printf("Device %d, byte %d, vtdr = 0x%x\n",
						ifc,
						byte_num,
						res_per_byte_vtdr[byte_num].reg);

				if(res_per_byte_vtdr[byte_num].fields.hvrefmx == 0x3F) {
					if(res_per_byte_vtdr[byte_num].fields.hvrefmn == 0)
						res_dx_n_gcr5[byte_num].reg = dx_n_gcr5.reg;
					else if (get_vref_index(phy_rd_vref_dq, ARRAY_SIZE(phy_rd_vref_dq),
							(current_ddr_params.phy_rd_vref * 10000) / MEM_VREF_MAX_VAL) >
							res_dx_n_gcr5[byte_num].fields.dxrefiselr0)
						res_dx_n_gcr5[byte_num].reg = dx_n_gcr5.reg;
				} else if (res_per_byte_vtdr[byte_num].fields.hvrefmn == 0){
					if (get_vref_index(phy_rd_vref_dq, ARRAY_SIZE(phy_rd_vref_dq),
							(current_ddr_params.phy_rd_vref * 10000) / MEM_VREF_MAX_VAL) <
									res_dx_n_gcr5[byte_num].fields.dxrefiselr0)
						res_dx_n_gcr5[byte_num].reg = dx_n_gcr5.reg;
				}
			}
		}

		for (byte_num = 0; byte_num < 4; byte_num++) {
			/* b 20 */
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_DX0GCR5_REG_ADDR + (byte_num * 0x40),
					res_dx_n_gcr5[byte_num].reg);

			dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc],
							PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
			dx_n_gcr0.fields.dxen = 1;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
					dx_n_gcr0.reg);
			if(get_debug())
				printf("Device %d, PHY selected VREF - byte %d = %d mV (index = %d)\n",
					ifc,
					byte_num,
					(phy_rd_vref_dq[res_dx_n_gcr5[byte_num].fields.dxrefiselr0] * 12) / 100,
					res_dx_n_gcr5[byte_num].fields.dxrefiselr0);
		}
		vtcr1.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
		vtcr1.fields.shrnk = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);

		bistar1.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);

		bistar1.fields.brank = 0;
		bistar1.fields.bmrank = 0xf;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTAR1_REG_ADDR, bistar1.reg);
#endif
#ifdef PER_BYTE
		/* per byte vref training operation */
		for(byte_num = 0; byte_num < 4; byte_num++) {
			dxngcr0 = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40));
			dxngcr0 &= 0xfffffffe;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40),
					dxngcr0);
		}
		for(byte_num = 0; byte_num < 4; byte_num++) {
			dxngcr0 = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40));
			dxngcr0 |= 0x1;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40),
					dxngcr0);

			bistrr.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_BISTRR_REG_ADDR);
			bistrr.fields.bdxsel = 0xf;//byte_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_BISTRR_REG_ADDR, bistrr.reg);

			bistar1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);
			bistar1.fields.brank = byte_num / 2;
			bistar1.fields.bmrank = byte_num / 2;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_BISTAR1_REG_ADDR, bistar1.reg);

			vtcr1.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
			vtcr1.fields.shrnk = byte_num / 2;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_VTCR1_REG_ADDR, vtcr1.reg);

			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_PIR_REG_ADDR, 0x00060001);
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(1000);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
								emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR);
				if ((pgsr0.fields.idone == 1) &&
					(pgsr0.fields.vdone == 1))
					break;
			}
			if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
				error("vref_training: timeout in host vref training in byte %d block %d, PGSR0 status = 0x%08X",
										byte_num, ifc, pgsr0.reg);
				print_fail_pub_dump(ifc);
				status = -EBUSY;
				continue;
			}

			if (!g_EZsim_mode)
				if (pgsr0.fields.verr) {
					error("vref_training: host vref training failed in byte %d  block %d, PGSR0 status = 0x%08X",
											byte_num, ifc, pgsr0.reg);
					print_fail_pub_dump(ifc);
					status = -EBUSY;
					continue;
				}
			dxngcr0 = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40));
			dxngcr0 &= 0xfffffffe;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40),
					dxngcr0);

			dxngcr5[byte_num] = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C5 + (byte_num * 0x40));

			dtcr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
			dtcr0.fields.dtdbs = byte_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_DTCR0_REG_ADDR, dtcr0.reg);
			per_byte_vtdr[byte_num].reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_VTDR_REG_ADDR);

			for(gcr5 = 0;gcr5 < 4; gcr5++) {
				val = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C5 + (gcr5 * 0x40));
				printf("dxngcr5 byte[%d] = 0x%x\n", gcr5, val);
			}
		}

		for(byte_num = 0; byte_num < 4; byte_num++) {
			printf("dxngcr5 byte[%d] = 0x%x,   hvrefmx = 0x%x,    hvrefmn = 0x%x\n",
								byte_num,
								dxngcr5[byte_num],
								per_byte_vtdr[byte_num].fields.hvrefmx,
								per_byte_vtdr[byte_num].fields.hvrefmn);
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], 0x1C5 + (byte_num * 0x40),
					dxngcr5[byte_num]);

			dxngcr0 = emem_mc_indirect_reg_read_synop(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40));
			dxngcr0 |= 0x1;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc], 0x1C0 + (byte_num * 0x40),
					dxngcr0);
		}
		vtcr1.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
		vtcr1.fields.shrnk = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);

		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bdxsel = 0xf;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		bistar1.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);

		bistar1.fields.brank = 0;
		bistar1.fields.bmrank = 0xf;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTAR1_REG_ADDR, bistar1.reg);
#endif
#if 0
		vtcr1.fields.shren = 1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);
#endif
		/* c */
		vtcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_VTCR0_REG_ADDR);
		vtcr0.fields.dven = 1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_VTCR0_REG_ADDR, vtcr0.reg);

		vtcr1.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
		vtcr1.fields.hven = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_VTCR1_REG_ADDR, vtcr1.reg);
		/* c.3 */
		for (byte_num = 0; byte_num < 4; byte_num++) {
			dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
			dx_n_gcr0.fields.dxen = 0;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
							dx_n_gcr0.reg);
		}
		/* c.4 */
		for(rank_num = 0; rank_num < 2; rank_num++) {
			invalid_vref_index = false;
			vtcr1.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
			vtcr1.fields.shrnk = rank_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_VTCR1_REG_ADDR, vtcr1.reg);

			bistar1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);
			bistar1.fields.brank = rank_num;
			bistar1.fields.bmrank = rank_num;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_BISTAR1_REG_ADDR, bistar1.reg);

			for (byte_num = rank_num * 2; byte_num < (rank_num * 2) + 2; byte_num++) {
				dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
				dx_n_gcr0.fields.dxen = 1;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
								dx_n_gcr0.reg);
			}
			/* c.10 */
			vref_dq_val = (current_ddr_params.mem_vref * 10000) / MEM_VREF_MAX_VAL;
			vtcr0.fields.dvinit = get_vref_index(vref_dq_2, ARRAY_SIZE(vref_dq_2), vref_dq_val);
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
								PUB_VTCR0_REG_ADDR, vtcr0.reg);

			mr6.fields.vrefdq_tr_rng = 1;
			mr6.fields.vrefdq_tr_en = 0;
			mr6.fields.vrefdq_tr_val = vtcr0.fields.dvinit;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
								PUB_MR6_REG_ADDR, mr6.reg);

			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_PIR_REG_ADDR, 0x00060001);
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(1000);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
								emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR);
				if ((pgsr0.fields.idone == 1) &&
					(pgsr0.fields.vdone == 1))
					break;
			}
			if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
				error("vref_training: timeout in sdram vref training range 2, in block %d, PGSR0 status = 0x%08X", ifc, pgsr0.reg);
				print_fail_pub_dump(ifc);
				status = -EBUSY;
				continue;
			}

			if (!g_EZsim_mode)
				if (pgsr0.fields.verr) {
					error("vref_training: sdram vref training range 2 failed, in block %d, PGSR0 status = 0x%08X", ifc, pgsr0.reg);
					print_fail_pub_dump(ifc);
					status = -EBUSY;
					continue;
				}
			/* c.15 */
			sdram_vref_min_index = 0;
			for(byte = 0;byte < 2; byte++) {
				dtcr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
				dtcr0.fields.dtdbs = (rank_num * 2) + byte;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_DTCR0_REG_ADDR, dtcr0.reg);
				udelay(10);
				vtdr.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTDR_REG_ADDR);
				if(vtdr.fields.dvrefmn > sdram_vref_min_index)
					sdram_vref_min_index = vtdr.fields.dvrefmn;
				if(sdram_vref_min_index > 0x32) {
					error("Invalid SDRAM minimum index (%d) byte %d device %d",
							sdram_vref_min_index,
							(rank_num * 2) + byte,
							ifc);
					invalid_vref_index = true;
				}
			}
			if(get_debug())
				printf("sdram_vref_min_index: %d rank %d\n",
					sdram_vref_min_index, rank_num);
			/* c.16 */
			mr6.fields.vrefdq_tr_val = get_vref_index(vref_dq_1, ARRAY_SIZE(vref_dq_1),
					(current_ddr_params.mem_vref * 10000) / MEM_VREF_MAX_VAL);
			vtcr0.reg = emem_mc_indirect_reg_read_synop(
							emem_mc_block_id[ifc], PUB_VTCR0_REG_ADDR);
			vtcr0.fields.dvinit = mr6.fields.vrefdq_tr_val;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
								PUB_VTCR0_REG_ADDR, vtcr0.reg);
			/* c.17 */
			mr6.fields.vrefdq_tr_rng = 0;
			mr6.fields.vrefdq_tr_en = 0;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
								PUB_MR6_REG_ADDR, mr6.reg);
			/* c.19 */
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_PIR_REG_ADDR, 0x00060001);
			for (i = 0 ; i < INDIRECT_RETRY_NUM; i++) {
				udelay(1000);
				pgsr0.reg = emem_mc_indirect_reg_read_synop(
								emem_mc_block_id[ifc], PUB_PGSR0_REG_ADDR);
				if ((pgsr0.fields.idone == 1) &&
					(pgsr0.fields.vdone == 1))
					break;
			}
			if (i == INDIRECT_RETRY_NUM && !g_EZsim_mode) {
				error("vref_training: timeout in sdram vref training range 1, in block %d, PGSR0 status = 0x%08X", ifc, pgsr0.reg);
				print_fail_pub_dump(ifc);
				status = -EBUSY;
				continue;
			}

			if (!g_EZsim_mode)
				if (pgsr0.fields.verr) {
					error("vref_training: sdram vref training range 1 failed, in block %d, PGSR0 status = 0x%08X", ifc, pgsr0.reg);
					print_fail_pub_dump(ifc);
					status = -EBUSY;
					continue;
				}
			/* c.21 */
			sdram_vref_max_index = 0xffffffff;
			for (byte = 0;byte < 2; byte++) {
				dtcr0.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
				dtcr0.fields.dtdbs = (rank_num * 2) + byte;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
							PUB_DTCR0_REG_ADDR, dtcr0.reg);
				udelay(10);
				vtdr.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTDR_REG_ADDR);
				if(vtdr.fields.dvrefmx < sdram_vref_max_index)
					sdram_vref_max_index = vtdr.fields.dvrefmx;
				if(sdram_vref_max_index > 0x32) {
					error("Invalid SDRAM maximum index (%d) byte %d device %d",
							sdram_vref_max_index,
							(rank_num * 2) + byte,
							ifc);
					invalid_vref_index = true;
				}
			}
			if(get_debug())
				printf("sdram_vref_max_index: %d rank %d\n",
					sdram_vref_max_index, rank_num);
			/* c.22 */
			for (byte = 0;byte < 2; byte++) {
				dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (((rank_num * 2) + byte) * 0x40));
				dx_n_gcr0.fields.dxen = 0;
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_DX0GCR0_REG_ADDR + (((rank_num * 2) + byte) * 0x40),
								dx_n_gcr0.reg);
			}
			/* c.25,26 */
			sdram_min_vref = vref_dq_2[sdram_vref_min_index];
			sdram_max_vref = vref_dq_1[sdram_vref_max_index];
			if(get_debug()) {
				printf("sdram_min_vref : %d rank %d\n",
						sdram_min_vref, rank_num);
				printf("sdram_max_vref : %d rank %d\n",
						sdram_max_vref, rank_num);
			}
			/* c. 27 */
			sdram_vref = (sdram_max_vref + sdram_min_vref) / 2;
			sdram_r1_vref_index  = get_vref_index(vref_dq_1, ARRAY_SIZE(vref_dq_1),
								sdram_vref);
			sdram_r2_vref_index  = get_vref_index(vref_dq_2, ARRAY_SIZE(vref_dq_2),
								sdram_vref);
			if(get_debug()) {
				printf("sdram_r2_vref_index : %d\n",
							sdram_r2_vref_index);
				printf("sdram_r1_vref_index : %d\n",
							sdram_r1_vref_index);
			}
			range1_margin = min((0x32 - sdram_r1_vref_index) , sdram_r1_vref_index);
			range2_margin = min((0x32 - sdram_r2_vref_index) , sdram_r2_vref_index);
			if(get_debug()) {
				printf("range1_margin : %d\n", range1_margin);
				printf("range2_margin : %d\n", range2_margin);
			}
			if(range1_margin > range2_margin) {
				sdram_vref_range = 0;
				sdram_vref_index = sdram_r1_vref_index;
			} else {
				sdram_vref_range = 1;
				sdram_vref_index = sdram_r2_vref_index;
			}
			if(get_debug()) {
				printf("sdram_vref_range : %d\n",
						sdram_vref_range);
				printf("sdram_vref_index : %d\n",
						sdram_vref_index);
			}
			if(invalid_vref_index ||
			(sdram_vref_max_index == 0x32 && sdram_vref_min_index == 0) ||
			(sdram_vref_max_index == 0x32 && sdram_vref_min_index != 0 &&
					mem_vref_init > sdram_vref) ||
			(sdram_vref_max_index != 0x32 && sdram_vref_min_index == 0 &&
					mem_vref_init < sdram_vref)) {
				sdram_vref_index = mr6_rank_vref[ifc][rank_num].fields.vrefdq_tr_val;
				sdram_vref_range = mr6_rank_vref[ifc][rank_num].fields.vrefdq_tr_rng;
			}
			/* c.29 */
			mr6.fields.vrefdq_tr_val = sdram_vref_index;
			mr6.fields.vrefdq_tr_rng = sdram_vref_range;
			mr6.fields.vrefdq_tr_en = 0;
			/* c.30 */
			mr6_rank_vref[ifc][rank_num].reg = mr6.reg;
			/* c.31 */
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
								PUB_MR6_REG_ADDR, mr6.reg);
			/* c.32 */
			mr6.fields.vrefdq_tr_en = 1;
			/* c.33 */
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[ifc], rank_num,
						GET_MRS_DATA_0(6, mr6.reg), GET_MRS_DATA_1(1));
			mr6.fields.vrefdq_tr_en = 0;
			udelay(1);
			/* c.35 */
			emem_mc_indirect_reg_write_mrs(emem_mc_block_id[ifc], rank_num,
					GET_MRS_DATA_0(6, mr6.reg), GET_MRS_DATA_1(1));
			udelay(1);
			emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[ifc], PUB_DX0GCR6_REG_ADDR);
			dx_x_gcr6.fields.dxdqvrefr0 = sdram_vref_index;
			dx_x_gcr6.fields.dxdqvrefr1 = sdram_vref_index;
			for(byte = 0; byte < 2; byte++)
				emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_DX0GCR6_REG_ADDR + (0x40 * ((rank_num * 2) + byte)),
							dx_x_gcr6.reg);
			if(get_debug())
				printf("Device %d, SDRAM selected VREF - Rank %d = %d mV (range = %d, index = %d)\n",
				ifc,
				rank_num,
				(sdram_vref_range == 0) ? ((vref_dq_1[sdram_vref_index] * 12) / 100):((vref_dq_2[sdram_vref_index] * 12) / 100),
				sdram_vref_range,
				sdram_vref_index);
		} /* rank loop */

		/* c.41 */
		for(byte_num = 0; byte_num < 4; byte_num++) {
			dx_n_gcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40));
			dx_n_gcr0.fields.dxen = 1;
			emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
				PUB_DX0GCR0_REG_ADDR + (byte_num * 0x40),
							dx_n_gcr0.reg);
		}
		vtcr1.reg = emem_mc_indirect_reg_read_synop(
						emem_mc_block_id[ifc], PUB_VTCR1_REG_ADDR);
		vtcr1.fields.shrnk = 0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
						PUB_VTCR1_REG_ADDR, vtcr1.reg);

		bistar1.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_BISTAR1_REG_ADDR);
		bistar1.fields.brank = 0;
		bistar1.fields.bmrank = 0xF;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
					PUB_BISTAR1_REG_ADDR, bistar1.reg);
		/* d */
		dtcr0.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[ifc], PUB_DTCR0_REG_ADDR);
		dtcr0.fields.rfshdt = 0x8;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[ifc],
				PUB_DTCR0_REG_ADDR, dtcr0.reg);
	}

	return status;
}

void print_failed_data(u8* err_data, u8* expected_data, int size_in_bytes, int mc)
{
	int byte, bit, index, bit_val = 0;
	struct dq_error_info timing_mask[DATA_QUEUES];
	char exp_ch, err_ch;
	const int byte_size = 8;

	memzero(timing_mask, DATA_QUEUES * sizeof(struct dq_error_info));

	for (byte = 0; byte < size_in_bytes; byte++) {
		if(err_data[byte] != expected_data[byte])
			break;
	}

	swap_line(&expected_data[byte/byte_size]);
	swap_line(&err_data[byte/byte_size]);
	for (index = 0; index < BYTES_TO_BITS(byte_size); index++) {
		exp_ch = expected_data[byte/byte_size + index/byte_size];
		err_ch = err_data[byte/byte_size + index/byte_size];
		bit = index % byte_size;
		if (GET_BIT(exp_ch, bit) != GET_BIT(err_ch, bit)) {
			bit_val = (1 << ((index/byte_size)  % 4));
			timing_mask[BIST_GET_DQ(index)].clocking |= bit_val;
			if(GET_BIT(exp_ch, bit))
				timing_mask[BIST_GET_DQ(index)].from |= bit_val;
			else
				timing_mask[BIST_GET_DQ(index)].to |= bit_val;
			bit_val = 0;
		}
	}

	for (index = 0; index < DATA_QUEUES; index++) {
		if (timing_mask[index].clocking != 0) {
			printf("\tDQ %2d (", index + (DATA_QUEUES * mc));
			if (GET_BIT(timing_mask[index].clocking, 0))
				printf(" First Rising %d->%d", 
				GET_BIT(timing_mask[index].from, 0),
				GET_BIT(timing_mask[index].to, 0));
			if (GET_BIT(timing_mask[index].clocking, 1))
				printf(" First Falling %d->%d",
				GET_BIT(timing_mask[index].from, 1),
				GET_BIT(timing_mask[index].to, 1));
			if (GET_BIT(timing_mask[index].clocking, 2))
				printf(" Second Rising %d->%d",
				GET_BIT(timing_mask[index].from, 2),
				GET_BIT(timing_mask[index].to, 2));
			if (GET_BIT(timing_mask[index].clocking, 3))
				printf(" Second Falling %d->%d",
				GET_BIT(timing_mask[index].from, 3),
				GET_BIT(timing_mask[index].to, 3));
			printf(")\n");
		}
	}
}

int post_ddr_training(u32 size)
{
	union emem_mc_ext_mc_latency ext_mc_latency;
	union emem_mc_mc_ddr_if mc_ddr_if;
	u32 block, bank, bank_group, row, col, wdata[8], rdata[8];
	u8 last_error_data[32], last_expected_data[32];
	u32 mc = 0, success = 0, i, curr_mc_latency = 0, bad_device = 0;
	int status = 0;
	const u32 retries = 20;

	if(get_debug())
		printf("=== post_ddr_training ===\n");

	ext_mc_latency.reg = 0;

	while (mc < (EMEM_MC_NUM_OF_BLOCKS * NUMBER_OF_MC_IN_BLOCK)) {
		if(SKIP_MC(mc)) {
			mc++;
			continue;
		}
		if(get_debug())
			printf("mc : %d\n", mc);
		block = mc / 2;
		curr_mc_latency = 10;
		success = 0;
		ext_mc_latency.reg = read_cached_register(EXT_MC_LATENCY_0 + block);
		if (GET_BIT(mc, 0) == 0)
			ext_mc_latency.fields.ext_mc_rl0 = 10;
		else
			ext_mc_latency.fields.ext_mc_rl1 = 10;
		write_cached_register((EXT_MC_LATENCY_0 + block), ext_mc_latency.reg);
		mc_ddr_if.reg = read_non_cluster_reg(emem_mc_block_id[block],
								EMEM_MC_REG_MC_DDR_IF);
		mc_ddr_if.fields.mc_en = (1 << (mc % NUMBER_OF_MC_IN_BLOCK));
		write_non_cluster_reg(emem_mc_block_id[block],
							EMEM_MC_REG_MC_DDR_IF, mc_ddr_if.reg);
		while ((success < retries) && (curr_mc_latency != 0)) {
			emem_mc_write_indirect_reg(emem_mc_block_id[block],
								EMEM_MC_IND_REG_MC_PUP_CTRL, 1,
								(1 << (mc % NUMBER_OF_MC_IN_BLOCK)),
								0, 0x301);
			emem_mc_write_indirect_reg(emem_mc_block_id[block],
							EMEM_MC_IND_REG_MC_PUP_CTRL, 1, 0x0, 0,
							0x301);
			udelay(5);
			bank = rand() % 4; /* TODO handle DDR3 */
			if (current_ddr_params.type == DDR4_8BIT)
				bank_group = rand() % 4;
			else
				bank_group = rand() % 2;

			row = rand() % get_max_row_num();
			col = rand() % 128;
			if(size == 32 )
				WRITE_BIT(col, 0, 0);

			if(get_debug()) {
				printf("\tbank = 0x%x\n", bank);
				printf("\tbank group = 0x%x\n", bank_group);
				printf("\trow = 0x%x\n", row);
				printf("\tcol = 0x%x\n", col);
			}
			for (i = 0; i < (size / 4); i++)
				wdata[i] = rand();
			write_sdram_chunk(0x1 << mc,
					bank_group, bank, row, col, size, wdata);
			if (g_ddr_pause) {
				printf("Write done: Press any key\n");
				getc();
			}
			read_sdram_chunk((0x1 << mc),
					bank_group, bank, row, col, size, rdata);
			if (g_ddr_pause) {
				printf("Read done: Press any key\n");
				getc();
			}
			bad_device = 0;
			if (memcmp(wdata, rdata, size) == 0) {
				success++;
			}
			else {
				if(!IS_16X_EXT_MEM_DEVICE(current_ddr_params.type)) {
					if(wdata[0] != rdata[0] || wdata[2] != rdata[2])
						bad_device |= 0x1;
					if(wdata[1] != rdata[1] || wdata[3] != rdata[3])
						bad_device |= 0x2;
				}
				if (GET_BIT(mc, 0) == 0) {
					ext_mc_latency.fields.ext_mc_rl0++;
					curr_mc_latency = ext_mc_latency.fields.ext_mc_rl0;
				}
				else {
					ext_mc_latency.fields.ext_mc_rl1++;
					curr_mc_latency = ext_mc_latency.fields.ext_mc_rl1;
				}
				write_cached_register((EXT_MC_LATENCY_0 + block),
							ext_mc_latency.reg);
				success = 0;
				memcpy(last_error_data, rdata, sizeof(last_error_data));
				memcpy(last_expected_data, wdata, sizeof(last_expected_data));
			}
		}
		if (GET_BIT(mc, 0) == 0) {
			ext_mc_latency.fields.ext_mc_rl0 += 3;
			if(success == retries) {
				if(get_debug())
					printf("Post training passed mc %d latency: %d\n",
						mc, ext_mc_latency.fields.ext_mc_rl0);
			} else {
				status = -1;
				printf("Post training failed. mc %d, ", mc);
				if(!IS_16X_EXT_MEM_DEVICE(current_ddr_params.type)) {
					if(GET_BIT(bad_device, 0))
						printf("device 0");
					if(GET_BIT(bad_device, 1))
						printf(" device 1");
					printf("\n");
				}
				else {
					printf("device 0\n");
				}
				print_failed_data(last_error_data, last_expected_data, size, mc%2);
			}
		} else {
			ext_mc_latency.fields.ext_mc_rl1 += 3;
			if(success == retries) {
				if(get_debug())
					printf("Post training passed mc %d latency: %d\n",
							mc, ext_mc_latency.fields.ext_mc_rl1);
			} else {
				status = -1;
				printf("Post training failed. mc %d, ", mc);
				if(!IS_16X_EXT_MEM_DEVICE(current_ddr_params.type)) {
					if(GET_BIT(bad_device, 0))
						printf("device 0");
					if(GET_BIT(bad_device, 1))
						printf(" device 1");
					printf("\n");
				}
				else {
					printf("device 0\n");
				}
				print_failed_data(last_error_data, last_expected_data, size, mc%2);
			}
		}
		write_cached_register((EXT_MC_LATENCY_0 + block),
						ext_mc_latency.reg);
		mc_ddr_if.reg = read_non_cluster_reg(emem_mc_block_id[block],
							EMEM_MC_REG_MC_DDR_IF);
		mc_ddr_if.fields.mc_en = 0;
		write_non_cluster_reg(emem_mc_block_id[block], /* (b) */
					EMEM_MC_REG_MC_DDR_IF, mc_ddr_if.reg);
		if (GET_BIT(mc, 0) == 1)
			ext_mc_latency.reg = 0;
		mc++;
	}

	return status;
}

static void set_err_indication(enum ddr_errors err_type)
{
	union  crg_gen_purp_0 gen_purp_0;

	gen_purp_0.reg = read_non_cluster_reg(CRG_BLOCK_ID, CRG_REG_GEN_PURP_0);
	gen_purp_0.fields.ddr_error = err_type;
	write_non_cluster_reg(CRG_BLOCK_ID, CRG_REG_GEN_PURP_0, gen_purp_0.reg);
}

static void ddr_phy_reset_internal_read_fifos(u32 port)
{
	union pub_pgcr0 pgcr0;

	/*1.*/
	pgcr0.reg = emem_mc_indirect_reg_read_synop(
			emem_mc_block_id[port], PUB_PGCR0_REG_ADDR);
	pgcr0.fields.phyfrst = 0;
	emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[port],	PUB_PGCR0_REG_ADDR, pgcr0.reg);
	/*2.*/
	udelay(1);

	/*3.*/
	pgcr0.reg = emem_mc_indirect_reg_read_synop(
			emem_mc_block_id[port], PUB_PGCR0_REG_ADDR);
	pgcr0.fields.phyfrst = 1;
	emem_mc_indirect_reg_write_synop_data0(
			emem_mc_block_id[port],	PUB_PGCR0_REG_ADDR, pgcr0.reg);

}

int ddr_int_loop(void)
{
	u32 port, reg_addr, retries;
	int global_status = 0;

	union crg_emi_rst emi_rst;
	union pub_pgcr1 pgcr1;
	union pub_bistmskr1 bistmskr1;
	union pub_bistrr bistrr;
	union pub_bistgsr bistgsr;

	union pub_int_lb_port_status int_lb_port_status;

	/* 1) */
	if (get_debug())
		printf("Step 1\n");
	set_ddr_freq();

	/* 2) */
	if (get_debug())
		printf("Step 2\n");
	for (reg_addr = CRG_REG_EMI_RST_START; reg_addr <= CRG_REG_EMI_RST_END ; reg_addr++) {
		emi_rst.reg = read_non_cluster_reg(CRG_BLOCK_ID, reg_addr);
		emi_rst.fields.umi_phy_rst_n = 1;
		write_non_cluster_reg(CRG_BLOCK_ID, reg_addr, emi_rst.reg);
	}

	/* 3) */
	if (get_debug())
		printf("Step 3\n");
	 apb_ifc_rst();

	/* 4) */
	if (get_debug())
		printf("Step 4\n");
	phy_init();

	for (port = 0; port < 12; port++) {

		int_lb_port_status.reg = 0;

		/* 5) */
		/* a) */
		if (get_debug())
			printf("Step 5, port %u\n", port);
		if (get_debug())
			printf("Step 5.a, port %u\n", port);
		pgcr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[port], PUB_PGCR1_REG_ADDR);
		pgcr1.fields.iolb = 1;
		pgcr1.fields.lbgdqs = 1;
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[port], PUB_PGCR1_REG_ADDR, pgcr1.reg);
		/* b) */
		if (get_debug())
			printf("Step 5.b, port %u\n", port);
		bistmskr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR);
		bistmskr1.fields.ckemsk = 2;
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR, bistmskr1.reg);

		/* c) */
		if (get_debug())
			printf("Step 5.c, port %u\n", port);
		bistmskr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR);
		bistmskr1.fields.odtmsk = 3;
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR, bistmskr1.reg);


		/* d) */
		if (get_debug())
			printf("Step 5.d, port %u\n", port);
		bistmskr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR);
		bistmskr1.fields.parinmsk = 1;
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR, bistmskr1.reg);


		/* e) */
		if (get_debug())
			printf("Step 5.e, port %u\n", port);
		bistmskr1.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR);
		bistmskr1.fields.dmmsk = 0xF;
		emem_mc_indirect_reg_write_synop_data0(
				emem_mc_block_id[port], PUB_BISTMSKR1_REG_ADDR, bistmskr1.reg);

		/* f) */
		if (get_debug())
			printf("Step 5.f, port %u\n", port);

		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bdpat = 0x2;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* g) */
		if (get_debug())
			printf("Step 5.g, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bacen = 0x1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* h) */
		if (get_debug())
			printf("Step 5.h, port %u\n", port);
		ddr_phy_reset_internal_read_fifos(port);

		/* i) */
		if (get_debug())
			printf("Step 5.i, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x3;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* j) */
		if (get_debug())
			printf("Step 5.j, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* k) */
		if (get_debug())
			printf("Step 5.k, port %u\n", port);
		for (retries = 0; retries < DDR_PHY_BIST_RETRY_NUM; retries++) {
			udelay(1);
			bistgsr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTGSR_REG_ADDR);
			if (bistgsr.fields.bdone == 1)
				break;
		}
		if (retries == DDR_PHY_BIST_RETRY_NUM && !g_EZsim_mode) {
			error("frst phy_int_lb_bist_run : retries exceeded (port %d)\n", port);
			int_lb_port_status.fields.frst_timeout = 1;;
		}

		/* l) */
		if (get_debug())
			printf("Step 5.l, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x2;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* m) */
		if (get_debug())
			printf("Step 5.m, port %u\n", port);
		bistgsr.reg = emem_mc_indirect_reg_read_synop(
			emem_mc_block_id[port], PUB_BISTGSR_REG_ADDR);
		if (bistgsr.fields.bacerr != 0) {
			error("frst phy_int_lb_bist_run: bacerr = %d (port %d)\n",
					bistgsr.fields.bacerr, port);
			int_lb_port_status.fields.frst_err = bistgsr.fields.bacerr;
		}

		/* n) */
		if (get_debug())
			printf("Step 5.n, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x3;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* o) */
		if (get_debug())
			printf("Step 5.o, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bacen = 0x0;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* p) */
		if (get_debug())
			printf("Step 5.p, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.bdxen = 0x1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* q) */
		if (get_debug())
			printf("Step 5.q, port %u\n", port);
		ddr_phy_reset_internal_read_fifos(port);


		/* r) */
		if (get_debug())
			printf("Step 5.r, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x1;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/* s) */
		if (get_debug())
			printf("Step 5.s, port %u\n", port);
		for (retries = 0; retries < DDR_PHY_BIST_RETRY_NUM; retries++) {
			udelay(1);
			bistgsr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTGSR_REG_ADDR);
			if (bistgsr.fields.bdone == 1)
				break;
		}
		if (retries == DDR_PHY_BIST_RETRY_NUM && !g_EZsim_mode) {
			error("scnd phy_int_lb_bist_run: retries exceeded (port %d)\n", port);
			int_lb_port_status.fields.scnd_timeout = 1;
		}

		/* t) */
		if (get_debug())
			printf("Step 5.t, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x2;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		/*u) */
		if (get_debug())
			printf("Step 5.u, port %u\n", port);
		bistgsr.reg = emem_mc_indirect_reg_read_synop(
			emem_mc_block_id[port], PUB_BISTGSR_REG_ADDR);
		if (bistgsr.fields.bdxerr != 0) {
			error("scnd phy_int_lb_bist_run: bdxerr = %d (port %d)\n",
					bistgsr.fields.bdxerr, port);
			int_lb_port_status.fields.scnd_err = (bistgsr.fields.bdxerr & 0xF); /* take only 4 bits */
		}

		/* v) */
		if (get_debug())
			printf("Step 5.v, port %u\n", port);
		bistrr.reg = emem_mc_indirect_reg_read_synop(
				emem_mc_block_id[port], PUB_BISTRR_REG_ADDR);
		bistrr.fields.binst = 0x3;
		emem_mc_indirect_reg_write_synop_data0(emem_mc_block_id[port],
					PUB_BISTRR_REG_ADDR, bistrr.reg);

		if (int_lb_port_status.reg) {
			printf("ERROR! INT_LB failed with status 0x%08X for port %d\n",
					int_lb_port_status.reg, port);
			global_status = 1;

		}
		else
			printf("INT_LB passed (status 0x%08X) for port %d\n",
					int_lb_port_status.reg, port);
	}
	return global_status;
}

static void clients_config(void)
{
	int reg, type = 0, i, block_id, size = 0x30E0;
	u32 bl_tap_ps;
	union ms_dfn_data_0 data_0;
	union ms_dfn_data_1 data_1;
	union pub_dx_n_bdlr2 dx_n_bdlr2;
	union pub_dx_x_mdlr0 dx_x_mdlr0;
	u32 tck_idnex = current_ddr_params.pll_freq;

	/* (4G + ECC) / ( devices * banks * 1K )
	 * for 16 banks and 24 devices the value
	 *  as calculated in CP is 0x030E0 */
	data_0.reg = 0x0E000000;
	data_1.reg = 0x33BABA03;

	if(GET_NUMBER_OF_BANKS(current_ddr_params.type) == 8) {
		size <<= 1;
		data_0.fields.size_lsb = GET_BITS(size, 0, 12);
		data_1.fields.size_msb = GET_BITS(size, 12, 20);
	}
	if (GET_NUMBER_OF_BANKS(current_ddr_params.type) == 16)
		type = 1;

	for (i = 0; i < L2C_NUM_OF_BLOCKS ; i++) {
		block_id = get_l2c_block_id(i);
		write_non_cluster_reg(block_id, L2C_REG_DDR_TYPE, type);
		write_non_cluster_reg(block_id, L2C_REG_IND_DATA0, data_0.reg);
		write_non_cluster_reg(block_id, L2C_REG_IND_DATA1, data_1.reg);
		write_non_cluster_reg(block_id, L2C_REG_IND_DATA2, 0x1);
		write_non_cluster_reg(block_id, L2C_REG_IND_ADDR, 0x32);
		write_non_cluster_reg(block_id, L2C_REG_IND_CMD, 0x2);
	}

	/* patch */
	for (i = 0; i < EMEM_MC_NUM_OF_BLOCKS ; i++) {
		for (reg = 0; reg < 4; reg++) {
			dx_x_mdlr0.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[i], PUB_DX0MDLR0_REG_ADDR + (reg * 0x40));
			bl_tap_ps = tCK2[tck_idnex] / (2 * dx_x_mdlr0.fields.iprd);
			dx_n_bdlr2.reg = emem_mc_indirect_reg_read_synop(
					emem_mc_block_id[i], PUB_DX0BDLR2_REG_ADDR + (reg * 0x40));
			dx_n_bdlr2.fields.dsoebd = DIV_ROUND_CLOSEST(98000, bl_tap_ps);
			emem_mc_indirect_reg_write_synop_data0(
					emem_mc_block_id[i], PUB_DX0BDLR2_REG_ADDR + (reg * 0x40),
					dx_n_bdlr2.reg);
		}
	}
}

int configure_emem(void)
{
	int status;
	bool env_ddr_relax = false, cur_debug = false;
	char *env_relax, *env_ddr;

	env_ddr = getenv("ddr_dump_verbose");
	if( env_ddr )
		g_ddr_dump_verbose = simple_strtoul(env_ddr, NULL, 10);
	env_ddr = getenv("ddr_htol_debug_verbose");
	if( env_ddr )
		g_ddr_htol_debug_verbose = simple_strtoul(env_ddr, NULL, 10);

	printf( "### g_ddr_dump_verbose = %u  g_ddr_htol_debug_verbose = %u\n", g_ddr_dump_verbose, g_ddr_htol_debug_verbose );

	env_relax = getenv("ddr_relax");
	if(env_relax)
		env_ddr_relax = (bool)simple_strtoul(env_relax, NULL, 10);

	init_ddr_phy_record_DB();
	get_ddr_parameters();
	/* set this mode from env */

	if (g_int_lb_mode) {
		printf("CALL ddr_int_loop\n");
		cur_debug = get_debug();
		status = ddr_int_loop();
		if (status)
			printf("ERROR!\n");
		else {
			printf("SUCCESS!\n");
		}
		printf("ddr_int_loop FINISHED\n");
		set_debug(cur_debug);
		return 0;
	}

	status = set_ddr_freq();
	if (status) {
		set_err_indication(PLL_LOCK_ERROR);
		printf("ERROR:set_ddr_freq failed\n");
		return -1;
	}
	configure_mc();
	if(env_ddr_relax)
		configure_relax_mc();
	status = apb_ifc_rst();
	if (status) {
		set_err_indication(PUB_CONECTION_ERROR);
		error("apb ifc reset failed");
		return -1;
	}
	configure_phy();
	status = phy_init();
	if (status) {
		set_err_indication(PHY_INIT_FAILED);
		error("Phy initialization failed");
		return -1;
	}
	if ( 1 ){ /* adding 1 sec delay to eliminate ddr errors after sw wl & dqs training */
		printf( "### delay 1 sec after Phy initialization\n" );
		mdelay( 1000 );
	}
	else{
		printf( "### NO DELAY after Phy initialization\n" );
	}

	if (!current_ddr_params.training_bypass) {
		status = phy_ck_setup();
		if (status) {
			set_err_indication(TAP_DELAY_0);
			error("Phy clock setup failed");
			return -1;
		}
	}
	sdram_init();

	if ( 0 ) { /* trial - to be removed? */
		ddr_int_loopback_test( 0x0);
		print_pub_dump(0);
		return -1;
	}


	if (!current_ddr_params.training_bypass) {
		status = ddr_training();
		if(status) {
			set_err_indication(DDR_TRAINING_FAILED);
			error("ddr training failed ");
			return -1;
		}
	}

	if(current_ddr_params.ddr_double_refresh) {
		set_ddr_double_refresh();
	}

	if (IS_DDR4_DEVICE(current_ddr_params.type)) {
		if(!getenv_yesno("ddr_vref_bypass")) {
			status = vref_training();
			if (status) {
				set_err_indication(VREF_TRAINING_FAILED);
				error("vref training failed ");
				return -1;
			}
		}
	}

	configure_crg_emi_rst(EMI_RST_PHASE_3);

	status = post_ddr_training(16);
	if (status) {
		set_err_indication(POST_DDR_TRAINING_FAILED);
		error("ddr post training failed ");
		return -1;
	}
	enable_ddr();
	set_full_ddr_size(current_ddr_params.size);
	if(!getenv_yesno("ddr_skip_bist")) {
		status = run_default_bist(GET_BITS(~skip_mc_mask, 0, EMEM_MC_NUM_OF_CONTROLLERS), current_ddr_params.size);
		if(status) {
			set_err_indication(BIST_FAILED);
			error("Default bist run failed");
			return -1;
		}
	}
        if(!getenv_yesno("ddr_skip_init")) {
		status = init_ddr_by_bist(GET_BITS(~skip_mc_mask, 0, EMEM_MC_NUM_OF_CONTROLLERS), current_ddr_params.size);
		if(status) {
			set_err_indication(BIST_FAILED);
			error("Default bist run failed");
			return -1;
		}
	}
	clients_config();
	set_err_indication(NO_ERROR);

	return 0;
}

int write_sdram_chunk(u32 mc_bitmask, u32 bg,
			u32 b, u32 row, u32 col, u32 size, u32 *data)
{
	u32 block, current, data_reg;

	for (block = 0; block < EMEM_MI_NUM_OF_BLOCKS; block++) {
		for (current = 0; current < NUMBER_OF_MC_IN_BLOCK; current++) {
			if (GET_BIT(mc_bitmask, (block * 2) + current) == 0)
				continue;
			for (data_reg = 0; data_reg < (size / 4); data_reg++) {
				write_non_cluster_reg(emem_mi_block_id[block],
						data_reg, data[data_reg]);
			}

			/* 32 bytes */
			if (size == 32) {
				write_non_cluster_reg(emem_mi_block_id[block],
							EMEM_MI_REG_IND_DATA0 + 8,
						((3 << (8 - (col % 8) - 1)) + current));
			} else {
			/* 16 bytes */
				write_non_cluster_reg(emem_mi_block_id[block],
							EMEM_MI_REG_IND_DATA0 + 8,
							((1 << (8 - (col % 8))) + current));
			}
			write_non_cluster_reg(emem_mi_block_id[block],
					EMEM_MI_REG_IND_DATA0 + 9,
					(row * 0x800) + (col * 0x10) + (bg * 0x4) + b);
			write_non_cluster_reg(emem_mi_block_id[block],
						EMEM_MI_REG_IND_CMD, 0x1000000 + 0xA);
			if (check_indirect_status(emem_mi_block_id[block]) != 0)
				error("Write to SDRAM failed at block %d\n",
									block);
		}
	}

	return 0;
}

int read_sdram_chunk(u32 mc_bitmask, u32 bg,
			u32 b, u32 row, u32 col, u32 size, u32 *data)
{
	u32 block, current, d_reg;
	u32 num;

	num = (size == 32)?8:4;

	for (block = 0; block < EMEM_MI_NUM_OF_BLOCKS; block++) {
		for (current = 0; current < NUMBER_OF_MC_IN_BLOCK; current++) {
			if (GET_BIT(mc_bitmask, (block * 2) + current) == 0)
				continue;
			for (d_reg = 0; d_reg < 8; d_reg++) {
				write_non_cluster_reg(
					emem_mi_block_id[block],
					EMEM_MI_REG_IND_DATA0 + d_reg, 0x0);
			}

			/* 32 bytes */
			if(size == 32) {
				write_non_cluster_reg(emem_mi_block_id[block],
							EMEM_MI_REG_IND_DATA0 + 8,
					((3 << (8 - (col % 8) - 1)) + current));
			} else {
				/* 16 bytes */
				write_non_cluster_reg(emem_mi_block_id[block],
							EMEM_MI_REG_IND_DATA0 + 8,
					((1 << (8 - (col % 8))) + current));
			}

			write_non_cluster_reg(emem_mi_block_id[block],
							EMEM_MI_REG_IND_DATA0 + 9,
				(row * 0x800) + (col * 0x10) + (bg * 0x4) + b);
			write_non_cluster_reg(emem_mi_block_id[block],
						EMEM_MI_REG_IND_CMD,
						0xA);
			if (check_indirect_status(emem_mi_block_id[block])
									!= 0)
				error("Read from SDRAM failed at block %d\n",
									block);
			else {
				for (d_reg = 0; d_reg < num; d_reg++) {
					data[d_reg] = read_non_cluster_reg(
						emem_mi_block_id[block],
						EMEM_MI_REG_IND_DATA0 + d_reg);
				}
			}
		}
	}

	return 0;
}

int emem_mc_indirect_reg_read_synop(u32 block, u32 address)
{
	u32 value;
	u32 elems, i;
	union pub_rankidr rankidr;

	if (BYTE_LANE_0_OR_1_PUB_REG(address)) {
		rankidr.reg = emem_mc_read_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 0x100, g_EZsim_mode);
		if (g_EZsim_mode) {
			if(get_debug())
				printf("R_PR A 0x%03X%03X D 0x%08X\n",
					block, PUB_RANKIDR_REG_ADDR,
					pub_regs_synop_val[0].val);
			rankidr.reg = pub_regs_synop_val[0].val;
		}
		if (rankidr.fields.rankrid != 0){
			rankidr.fields.rankrid = 0;
			emem_mc_indirect_reg_write_synop_data0(
				block, PUB_RANKIDR_REG_ADDR, rankidr.reg);
			udelay(1);
		}
	} else if (BYTE_LANE_2_OR_3_PUB_REG(address)) {
		rankidr.reg = emem_mc_read_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 0x100, g_EZsim_mode);
		if (g_EZsim_mode) {
			if(get_debug())
				printf("R_PR A 0x%03X%03X D 0x%08X\n",
					block, PUB_RANKIDR_REG_ADDR,
					pub_regs_synop_val[0].val);
			rankidr.reg = pub_regs_synop_val[0].val;
		}
		if (rankidr.fields.rankrid != 1){
			rankidr.fields.rankrid = 1;
			emem_mc_indirect_reg_write_synop_data0(
				block, PUB_RANKIDR_REG_ADDR, rankidr.reg);
			udelay(1);
		}
	}

	value = emem_mc_read_indirect_reg(block, address, 0x100, g_EZsim_mode);
	if (g_EZsim_mode) {
		elems = ARRAY_SIZE(pub_regs_synop_val);
		for (i = 0; i < elems; i++) {
			if (pub_regs_synop_val[i].address == address) {
				if(get_debug())
					printf("R_PR A 0x%03X%03X D 0x%08X\n",
						block, address,
						pub_regs_synop_val[i].val);
				return pub_regs_synop_val[i].val;
			}
		}

		printf("ERROR: add reg address 0x%08X to synop DB\n", address);
		return 0;
	}
	return value;
}

void emem_mc_indirect_reg_write_synop_data0(u32 block, u32 address, u32 data_0)
{
	u32 elems, i;
	union pub_rankidr rankidr;

	if (BYTE_LANE_0_OR_1_PUB_REG(address)) {
		rankidr.reg = emem_mc_read_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 0x100, g_EZsim_mode);
		if (g_EZsim_mode) {
			if(get_debug())
				printf("R_PR A 0x%03X%03X D 0x%08X\n",
					block, PUB_RANKIDR_REG_ADDR,
					pub_regs_synop_val[0].val);
			rankidr.reg = pub_regs_synop_val[0].val;
		}
		if (rankidr.fields.rankwid != 0){
			rankidr.fields.rankwid = 0;
			emem_mc_write_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 1, rankidr.reg, 0, 0x101);
			pub_regs_synop_val[0].val = rankidr.reg;
			udelay(1);
		}
	} else if (BYTE_LANE_2_OR_3_PUB_REG(address)){
		rankidr.reg = emem_mc_read_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 0x100, g_EZsim_mode);
		if (g_EZsim_mode) {
			if (get_debug())
				printf("R_PR A 0x%03X%03X D 0x%08X\n",
					block, PUB_RANKIDR_REG_ADDR,
					pub_regs_synop_val[0].val);
			rankidr.reg = pub_regs_synop_val[0].val;
		}
		if (rankidr.fields.rankwid != 1) {
			rankidr.fields.rankwid = 1;
			emem_mc_write_indirect_reg(block, PUB_RANKIDR_REG_ADDR, 1, rankidr.reg, 0, 0x101);
			pub_regs_synop_val[0].val = rankidr.reg;
			udelay(1);
		}
	}

	emem_mc_write_indirect_reg(block, address, 1, data_0, 0, 0x101);

	if (g_EZsim_mode) {
		elems = ARRAY_SIZE(pub_regs_synop_val);
		for (i = 0; i < elems; i++) {
			if (pub_regs_synop_val[i].address == address) {
				pub_regs_synop_val[i].val = data_0;
				return;
			}
		}

		printf("ERROR: add reg address 0x%x to synop DB\n", address);
		return;
	}
}

int do_sw_wl(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int ret;
	u32 wl_mr1_data;

	wl_mr1_data = simple_strtol(argv[1], NULL, 16);
	ret = sw_write_leveling(0, wl_mr1_data);
	if (ret) {
		error("sw write leveling failed");
		return ret;
	}
	printf("sw write leveling passed\n");

	return 0;
}

int do_calib_fail_recovery(cmd_tbl_t *cmdtp, int flag, int argc,
							char * const argv[])
{
	int ret;
	ret = dll_calibration_fail_recovery(0);
	if (ret) {
		error("calibration_fail_recovery failed");
		return ret;
	}
	printf("calibration_fail_recovery passed\n");

	return 0;
}


#ifdef CONFIG_NPS_DDR_DEBUG
int do_print_ddr_config(cmd_tbl_t *cmdtp, int flag, int argc,
							char * const argv[])
{
	printf("\tType & package:\t");
	switch(current_ddr_params.type) {
	case(DDR3_8BIT):
		printf("DDR3_x8\n");
		break;
	case(DDR3_16BIT):
		printf("DDR3_x16\n");
		break;
	case(DDR4_8BIT):
		printf("DDR4_x8\n");
		break;
	case(DDR4_16BIT):
		printf("DDR4_x16\n");
		break;
	default:
		printf("Invalid DDR type\n");
		break;
	}

	printf("\tTotal DDR Size:\t %dMBytes\n",current_ddr_params.size);
	printf("\tClock :\t %dMHz\n",current_ddr_params.clock_frequency);

	return 0;
}
#endif
