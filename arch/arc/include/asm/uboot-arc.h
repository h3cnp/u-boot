/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _U_BOOT_ARC_H_
#define _U_BOOT_ARC_H_  1

extern ulong _arcboot_start; /* code start */
extern ulong _bss_start;     /* bss start */
extern ulong _bss_end;       /* bss end	  */

int     cpu_init(void);
void _invalidate_dcache(void);

/* cpu/.../cpu.c */
int	cpu_init(void);
int	cleanup_before_linux(void);

/* cpu/.../arch/cpu.c */
int	arch_cpu_init(void);
int	arch_misc_init(void);

/* board/.../... */
int	board_init(void);
int	dram_init(void);

/* ------------------------------------------------------------
 * Here is a list of some prototypes which are incompatible to
 * the U-Boot implementation
 * To be fixed!
 * ------------------------------------------------------------ */

/* common/cmd_nvedit.c */

/* cpu/.../interrupt.c */
int   arch_interrupt_init(void);
void  reset_timer_masked(void);
ulong get_timer_masked(void);
void  udelay_masked(unsigned long usec);

/* cpu/.../timer.c */
int	timer_init(void);

#endif /* _U_BOOT_ARC_H_ */
