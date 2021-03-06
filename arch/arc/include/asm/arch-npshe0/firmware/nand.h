/*
 * Copyright (C) 2012 EZchip, Inc. (www.ezchip.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef NAND_H_INCLUDED
#define NAND_H_INCLUDED

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <common.h>

/* Register defination */

#define NAND_SYS_CLK_NAME  "clk81"
#define NAND_CYCLE_DELAY   75
#define NAND_BOOT_NAME     "nand0"
#define NAND_NORMAL_NAME   "nand1"
#define NAND_MULTI_NAME    "nand2"

#define A3_BOOT_WRITE_SIZE      0x600
#define A3_BOOT_COPY_NUM        4
#define A3_BOOT_PAGES_PER_COPY  256

#define NFC_BASE      CBUS_REG_ADDR(NAND_CMD)
#define NFC_OFF_CMD   ((NAND_CMD  - NAND_CMD) << 2)
#define NFC_OFF_CFG   ((NAND_CFG  - NAND_CMD) << 2)
#define NFC_OFF_DADR  ((NAND_DADR - NAND_CMD) << 2)
#define NFC_OFF_IADR  ((NAND_IADR - NAND_CMD) << 2)
#define NFC_OFF_BUF   ((NAND_BUF  - NAND_CMD) << 2)
#define NFC_OFF_INFO  ((NAND_INFO - NAND_CMD) << 2)
#define NFC_OFF_DC    ((NAND_DC   - NAND_CMD) << 2)
#define NFC_OFF_ADR   ((NAND_ADR  - NAND_CMD) << 2)
#define NFC_OFF_DL    ((NAND_DL   - NAND_CMD) << 2)
#define NFC_OFF_DH    ((NAND_DH   - NAND_CMD) << 2)

/*
 * Common Nand Read Flow
 */
#define CE0  (0xe << 10)
#define CE1  (0xd << 10)
#define CE2  (0xb << 10)
#define CE3  (0x7 << 10)
#define CE_NOT_SEL  (0xf << 10)
#define IO4  ((0xe << 10) | (1 << 18))
#define IO5  ((0xd << 10) | (1 << 18))
#define IO6  ((0xb << 10) | (1 << 18))
#define CLE  (0x5 << 14)
#define ALE  (0x6 << 14)
#define DWR  (0x4 << 14)
#define DRD  (0x8 << 14)
#define IDLE (0xc << 14)
#define RB   (1 << 20)

#define PER_INFO_BYTE 8
#define SIZE_INT sizeof(unsigned int)

#define M2N  ((0 << 17) | (2 << 20) | (1 << 19))
#define N2M  ((1 << 17) | (2 << 20) | (1 << 19))

#define M2N_NORAN  0x00200000
#define N2M_NORAN  0x00220000

#define STS  ((3 << 17) | (2 << 20))
#define ADL  ((0 << 16) | (3 << 20))
#define ADH  ((1 << 16) | (3 << 20))
#define AIL  ((2 << 16) | (3 << 20))
#define AIH  ((3 << 16) | (3 << 20))
#define ASL  ((4 << 16) | (3 << 20))
#define ASH  ((5 << 16) | (3 << 20))
#define SEED ((8 << 16) | (3 << 20))

/*
 * Nand Flash Controller (M1)
 * Global Macros
 */

/*
 * Config Group
 */
#define NFC_SET_CMD_START()  SET_CBUS_REG_MASK(NAND_CFG, 1 << 12)
#define NFC_SET_CMD_AUTO()   SET_CBUS_REG_MASK(NAND_CFG, 1 << 13)
#define NFC_SET_STS_IRQ(en)  WRITE_CBUS_REG_BITS(NAND_CFG, en, 20, 1)
#define NFC_SET_CMD_IRQ(en)  WRITE_CBUS_REG_BITS(NAND_CFG, en, 21, 1)
#define NFC_SET_TIMING_ASYC(bus_tim, bus_cyc) \
	WRITE_CBUS_REG_BITS(NAND_CFG, ((bus_cyc&31) | ((bus_tim & 31) << 5) |\
		(0 << 10)) , 0, 12)
#define NFC_SET_TIMING_SYNC(bus_tim, bus_cyc, sync_mode) \
	WRITE_CBUS_REG_BITS(NAND_CFG, (bus_cyc & 31) | ((bus_tim & 31) << 5) |\
		((sync_mode & 2) << 10), 0, 12)
#define NFC_SET_TIMING_SYNC_ADJUST()
#define NFC_SET_DMA_MODE(is_apb, spare_only) \
	WRITE_CBUS_REG_BITS(NAND_CFG, ((spare_only << 1) | (is_apb)), 14, 2)

/*
 * CMD relative Macros
 * Shortage word . NFCC
 */

#define NFC_CMD_IDLE(ce, time) ((ce) | IDLE | (time & 0x3ff))
#define NFC_CMD_CLE(ce, cmd) ((ce) | CLE | (cmd  & 0x0ff))
#define NFC_CMD_ALE(ce, addr) ((ce) | ALE | (addr & 0x0ff))
#define NFC_CMD_STANDBY(time) (STANDBY | (time  & 0x3ff))
#define NFC_CMD_ADL(addr) (ADL | (addr & 0xffff))
#define NFC_CMD_ADH(addr) (ADH | ((addr >> 16) & 0xffff))
#define NFC_CMD_AIL(addr) (AIL | (addr & 0xffff))
#define NFC_CMD_AIH(addr) (AIH | ((addr >> 16) & 0xffff))
#define NFC_CMD_DWR(data) (DWR | (data & 0xff))
#define NFC_CMD_DRD(ce, size) (ce | DRD | size)
#define NFC_CMD_RB(ce, time) ((ce) | RB | (time & 0x1f))
#define NFC_CMD_RB_INT(ce, time) ((ce) | RB | (((ce >> 10) ^ 0xf) << 14) |\
	(time & 0x1f))
#define NFC_CMD_RBIO(time, io) (RB | io | (time & 0x1f))
#define NFC_CMD_RBIO_INT(io, time) (RB | (((io >> 10) ^ 0x7) << 14) |\
	(time & 0x1f))
#define NFC_CMD_SEED(seed) (SEED | seed & 0x7fff)
#define NFC_CMD_STS(tim) (STS | (tim & 3))
#define NFC_CMD_M2N(ran, ecc, sho, pgsz, pag) \
	((ran ? M2N : M2N_NORAN) | (ecc << 14) | (sho << 13) |\
		((pgsz & 0x7f) << 6) | (pag & 0x3f))
#define NFC_CMD_N2M(ran, ecc, sho, pgsz, pag) \
	((ran ? N2M : N2M_NORAN) | (ecc << 14) | (sho << 13) |\
		((pgsz & 0x7f) << 6) | (pag & 0x3f))

/*
 * Alias for CMD
 */
#define NFC_CMD_D_ADR(addr)  NFC_CMD_ADL(addr), NFC_CMD_ADH(addr)
#define NFC_CMD_I_ADR(addr)  NFC_CMD_ADI(addr), NFC_CMD_ADI(addr)

/*
 * Register Operation and Controller Status
 */
#define NFC_SEND_CMD(cmd)	(WRITE_CBUS_REG(NAND_CMD, cmd))
#define NFC_READ_INFO()	  (READ_CBUS_REG(NAND_CMD))

/*
 * ECC defination(M1)
 */
#define NAND_ECC_NONE			 (0x0)
#define NAND_ECC_BCH8_512		 (0x1)
#define NAND_ECC_BCH8_1K		  (0x2)
#define NAND_ECC_BCH16			(0x3)
#define NAND_ECC_BCH24			(0x4)
#define NAND_ECC_BCH30			  (0x5)
#define NAND_ECC_BCH40			  (0x6)
#define NAND_ECC_BCH60			  (0x7)

/*
 * Cmd FIFO control
 */
#define NFC_CMD_FIFO_GO()	 (WRITE_CBUS_REG(NAND_CMD, (1 << 30)))
#define NFC_CMD_FIFO_RESET()  (WRITE_CBUS_REG(NAND_CMD, (1 << 31)))

/*
 * ADDR operations
 */
#define NFC_SET_DADDR(a)  (WRITE_CBUS_REG(NAND_DADR, (unsigned)a))
#define NFC_SET_IADDR(a)  (WRITE_CBUS_REG(NAND_IADR, (unsigned)a))
#define NFC_SET_SADDR(a)  (WRITE_CBUS_REG(NAND_SADR, (unsigned)a))

/*
 * Send command directly
 */
#define NFC_SEND_CMD_IDLE(ce, time) NFC_SEND_CMD(NFC_CMD_IDLE(ce, time))
#define NFC_SEND_CMD_CLE(ce, cmd) NFC_SEND_CMD(NFC_CMD_CLE(ce, cmd))
#define NFC_SEND_CMD_ALE(ce, addr) NFC_SEND_CMD(NFC_CMD_ALE(ce, addr))
#define NFC_SEND_CMD_STANDBY(time) NFC_SEND_CMD(NFC_CMD_STANDBY(time))
#define NFC_SEND_CMD_ADL(addr) NFC_SEND_CMD(NFC_CMD_ADL(addr))
#define NFC_SEND_CMD_ADH(addr) NFC_SEND_CMD(NFC_CMD_ADH(addr))
#define NFC_SEND_CMD_AIL(addr) NFC_SEND_CMD(NFC_CMD_AIL(addr))
#define NFC_SEND_CMD_AIH(addr) NFC_SEND_CMD(NFC_CMD_AIH(addr))
#define NFC_SEND_CMD_DWR(data) NFC_SEND_CMD(NFC_CMD_DWR(data))
#define NFC_SEND_CMD_DRD(ce, size) NFC_SEND_CMD(NFC_CMD_DRD(ce, size))
#define NFC_SEND_CMD_RB(ce, time) NFC_SEND_CMD(NFC_CMD_RB(ce, time))
#define NFC_SEND_CMD_SEED(seed) NFC_SEND_CMD(NFC_CMD_SEED(seed))
#define NFC_SEND_CMD_M2N(ran, ecc, sho, pgsz, pag)\
	NFC_SEND_CMD(NFC_CMD_M2N(ran, ecc, sho, pgsz, pag))
#define NFC_SEND_CMD_N2M(ran, ecc, sho, pgsz, pag)\
	NFC_SEND_CMD(NFC_CMD_N2M(ran, ecc, sho, pgsz, pag))

#define NFC_SEND_CMD_M2N_RAW(ran, len)\
	NFC_SEND_CMD((ran ? M2N : M2N_NORAN) | (len & 0x3fff))
#define NFC_SEND_CMD_N2M_RAW(ran, len)\
	NFC_SEND_CMD((ran ? N2M : N2M_NORAN) | (len & 0x3fff))

/*
 * Cmd Info Macros
 */
#define NFC_INFO_GET() (READ_CBUS_REG(NAND_CMD))
#define NFC_CMDFIFO_SIZE() ((NFC_INFO_GET() >> 22) & 0x1f)
#define NFC_CHECEK_RB_TIMEOUT() ((NFC_INFO_GET() >> 27) & 0x1)
#define NFC_GET_RB_STATUS(ce) (((NFC_INFO_GET() >> 28) & (~(ce >> 10))) & 0xf)
#define NFC_GET_BUF() READ_CBUS_REG(NAND_BUF)
#define NFC_SET_CFG(val) (WRITE_CBUS_REG(NAND_CFG, (unsigned)val))
#define NFC_FIFO_CUR_CMD() ((NFC_INFO_GET() >> 22) & 0x3FFFFF)

#define NAND_INFO_DONE(a) (((a) >> 31) & 1)
#define NAND_ECC_ENABLE(a) (((a) >> 30) & 1)
#define NAND_ECC_CNT(a) (((a) >> 24) & 0x3f)
#define NAND_ZERO_CNT(a) (((a) >> 16) & 0x3f)
#define NAND_INFO_DATA_2INFO(a) ((a) & 0xffff)
#define NAND_INFO_DATA_1INFO(a) ((a) & 0xff)

#define NAND_DEFAULT_OPTIONS (NAND_TIMING_MODE5 | NAND_ECC_BCH8_512_MODE)

#define ARC_NORMAL               0
#define ARC_MULTI_CHIP           1
#define ARC_MULTI_CHIP_SHARE_RB  2
#define ARC_INTERLEAVING_MODE    4

#define ARC_NAND_CE0  0xe
#define ARC_NAND_CE1  0xd
#define ARC_NAND_CE2  0xb
#define ARC_NAND_CE3  0x7

#define ARC_BADBLK_POS					0
#define NAND_ECC_UNIT_SIZE				512
#define NAND_ECC_UNIT_1KSIZE			1024
#define NAND_ECC_UNIT_SHORT				384

#define NAND_BCH8_512_ECC_SIZE			14
#define NAND_BCH8_1K_ECC_SIZE			14
#define NAND_BCH16_ECC_SIZE				28
#define NAND_BCH24_ECC_SIZE				42
#define NAND_BCH30_ECC_SIZE				54
#define NAND_BCH40_ECC_SIZE				70
#define NAND_BCH60_ECC_SIZE				106

#define NAND_ECC_OPTIONS_MASK			0x0000000f
#define NAND_PLANE_OPTIONS_MASK			0x000000f0
#define NAND_TIMING_OPTIONS_MASK		0x00000f00
#define NAND_BUSW_OPTIONS_MASK			0x0000f000
#define NAND_INTERLEAVING_OPTIONS_MASK	0x000f0000

#define NAND_ECC_SOFT_MODE				0x00000000
#define NAND_ECC_BCH8_512_MODE			0x00000001
#define NAND_ECC_BCH8_1K_MODE			0x00000002
#define NAND_ECC_BCH16_MODE				0x00000003
#define NAND_ECC_BCH24_MODE				0x00000004
#define NAND_ECC_BCH30_MODE				0x00000005
#define NAND_ECC_BCH40_MODE				0x00000006
#define NAND_ECC_BCH60_MODE				0x00000007
#define NAND_ECC_SHORT_MODE				0x00000008

#define NAND_TWO_PLANE_MODE				0x00000010
#define NAND_INTERLEAVING_MODE			0x00010000
#define NAND_TIMING_MODE0				0x00000000
#define NAND_TIMING_MODE1				0x00000100
#define NAND_TIMING_MODE2				0x00000200
#define NAND_TIMING_MODE3				0x00000300
#define NAND_TIMING_MODE4				0x00000400
#define NAND_TIMING_MODE5				0x00000500

#define ARC_NAND_BUSY_TIMEOUT			0x40000
#define ARC_DMA_BUSY_TIMEOUT			0x100000
#define MAX_ID_LEN						8

#define NAND_CMD_PLANE2_READ_START		0x06
#define NAND_CMD_TWOPLANE_PREVIOS_READ	0x60
#define NAND_CMD_TWOPLANE_READ1			0x5a
#define NAND_CMD_TWOPLANE_READ2			0xa5
#define NAND_CMD_TWOPLANE_WRITE2_MICRO	0x80
#define NAND_CMD_TWOPLANE_WRITE2		0x81
#define NAND_CMD_DUMMY_PROGRAM			0x11
#define NAND_CMD_ERASE1_END				0xd1
#define NAND_CMD_MULTI_CHIP_STATUS		0x78

#define MAX_CHIP_NUM		4
#define USER_BYTE_NUM		4

#define NAND_STATUS_READY_MULTI			0x20

struct arc_nand_flash_dev {
	char *name;
	u8 id[MAX_ID_LEN];
	unsigned pagesize;
	unsigned chipsize;
	unsigned erasesize;
	unsigned oobsize;
	unsigned internal_chipnr;
	unsigned options;
};

struct ecc_desc_s {
	char *name;
	unsigned bch;
	unsigned size;
	unsigned parity;
	unsigned user;
};

struct arc_nand_chip {
	/* mtd info */
	u8 mfr_type;
	unsigned options;
	unsigned page_size;
	unsigned block_size;
	unsigned oob_size;
	unsigned virtual_page_size;
	unsigned virtual_block_size;
	u8 plane_num;
	u8 chip_num;
	u8 internal_chipnr;
	unsigned internal_page_nums;

	unsigned	 internal_chip_shift;
	unsigned int ran_mode; /* def close, for all part */
	unsigned int rbpin_mode;
	unsigned int short_pgsz; /* zero means no short */

	unsigned bch_mode;
	u8 user_byte_mode;
	u8 ops_mode;
	u8 cached_prog_status;
	unsigned chip_enable[MAX_CHIP_NUM];
	unsigned rb_enable[MAX_CHIP_NUM];
	unsigned chip_selected;
	unsigned rb_received;
	unsigned valid_chip[MAX_CHIP_NUM];
	unsigned page_addr;
	unsigned char *arc_nand_data_buf;
	unsigned int *user_info_buf;

	struct mtd_info			mtd;
	struct nand_chip		chip;

	/* platform info */
	struct arc_nand_platform	*platform;

	/* device info */
	struct device			*device;

	unsigned max_ecc;
	struct ecc_desc_s *ecc;

	/* plateform operation function */
	void (*arc_nand_hw_init)(struct arc_nand_chip *arc_chip);
	int  (*arc_nand_options_confirm)(struct arc_nand_chip *arc_chip);
	void (*arc_nand_cmd_ctrl)(struct arc_nand_chip *arc_chip, int cmd,
		unsigned int ctrl);
	void (*arc_nand_select_chip)(struct arc_nand_chip *arc_chip,
		int chipnr);
	void (*arc_nand_get_user_byte)(struct arc_nand_chip *arc_chip,
		unsigned char *oob_buf, int byte_num);
	void (*arc_nand_set_user_byte)(struct arc_nand_chip *arc_chip,
		unsigned char *oob_buf, int byte_num);
	void (*arc_nand_command)(struct arc_nand_chip *arc_chip,
		unsigned command, int column, int page_addr, int chipnr);
	int  (*arc_nand_wait_devready)(struct arc_nand_chip *arc_chip,
		int chipnr);
	int  (*arc_nand_dma_read)(struct arc_nand_chip *arc_chip,
		unsigned char *buf, int len, unsigned bch_mode);
	int  (*arc_nand_dma_write)(struct arc_nand_chip *arc_chip,
		unsigned char *buf, int len, unsigned bch_mode);
	int  (*arc_nand_hwecc_correct)(struct arc_nand_chip *arc_chip,
		unsigned char *buf, unsigned size, unsigned char *oob_buf);
};

struct arc_nand_platform {
	struct arc_nand_flash_dev *nand_flash_dev;
	char *name;
	unsigned chip_enable_pad;
	unsigned ready_busy_pad;

	/* DMA RD/WR delay loop  timing */
	unsigned int T_REA;	   /* for dma  wating delay */
	unsigned int T_RHOH;	  /* not equal to nandchip->delay */
	unsigned int ran_mode;	/* def close, for all part */
	unsigned int rbpin_mode;  /* may get from romboot */
	unsigned int short_pgsz;  /* zero means no short */

	struct arc_nand_chip  *arc_chip;
	struct platform_nand_data platform_nand_data;
};

struct arc_nand_device {
	struct arc_nand_platform *arc_nand_platform;
	u8 dev_num;
};

static inline void nand_get_chip(void)
{
	SET_CBUS_REG_MASK(PERIPHS_PIN_MUX_4, (0x1 << 11) | (0xff << 14));
}

static inline void nand_release_chip(void)
{
	CLEAR_CBUS_REG_MASK(PERIPHS_PIN_MUX_4, (0x1 << 11) | (0xff << 14));
}

static inline struct arc_nand_chip *mtd_to_nand_chip(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

	return chip->priv;
}

extern int arc_nand_init(struct arc_nand_chip *arc_chip);

#endif /* NAND_H_INCLUDED */
