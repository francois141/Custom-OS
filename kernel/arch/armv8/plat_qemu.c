/**
 * \file plat_arm_vm.c
 * \brief
 */


/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <kernel.h>
#include <offsets.h>
#include <arch/arm/platform.h>
#include <serial.h>
#include <arch/arm/pl011.h>
#include <arch/arm/gic.h>

#include <getopt/getopt.h>
// #include <dev/apm88xxxx/apm88xxxx_pc16550_dev.h>
// #include <arch/arm/gic.h>
#include <sysreg.h>
#include <dev/armv8_dev.h>
#include <barrelfish_kpi/arm_core_data.h>
#include <psci.h>
#include <arch/armv8/global.h>
// #include <arch/armv8/gic_v3.h>
// #include <paging_kernel_arch.h>

/* RAM starts at 0, provided by the MMAP */
lpaddr_t phys_memory_start= 0;

/*
 * ----------------------------------------------------------------------------
 * GIC
 * ----------------------------------------------------------------------------
 */

lpaddr_t platform_gic_distributor_base = 0x8000000;
lpaddr_t platform_gic_redistributor_base = 0x80a0000;

/*
 * ----------------------------------------------------------------------------
 * UART
 * ----------------------------------------------------------------------------
 */

/* the maximum number of UARTS supported */
#define MAX_NUM_UARTS 1

/* the serial console port */
unsigned int serial_console_port = 0;

/* the debug console port */
unsigned int serial_debug_port = 0;

/* the number of physical ports */
unsigned serial_num_physical_ports = 1;

/* uart bases */
lpaddr_t platform_uart_base[MAX_NUM_UARTS] =
{
        0x9000000
};

/* uart sizes */
size_t platform_uart_size[MAX_NUM_UARTS] =
{
    4096
};

errval_t serial_init(unsigned port, bool initialize_hw)
{
    lvaddr_t base = local_phys_to_mem(platform_uart_base[port]);
    pl011_init(port, base, initialize_hw);
    return SYS_ERR_OK;
};

/*
 * Do any extra initialisation for this particular CPU (e.g. A9/A15).
 */
void platform_revision_init(void)
{

}

/*
 * Figure out how much RAM we have
 */
size_t platform_get_ram_size(void)
{
    return 0;
}

/*
 * Boot secondary processors
 */
errval_t platform_boot_core(hwid_t target, genpaddr_t gen_entry, genpaddr_t context)
{
    printf("Invoking PSCI on: cpu=0x%lx, entry=0x%lx, context=0x%lx\n", target, gen_entry, context);
    struct armv8_core_data *cd = (struct armv8_core_data *)local_phys_to_mem(context);
    cd->page_table_root = armv8_TTBR1_EL1_rd(NULL);
    cd->cpu_driver_globals_pointer = (uintptr_t)global;
    __asm volatile("dsb   sy\n"
                   "dmb   sy\n"
                   "isb     \n");
    return psci_cpu_on(target, gen_entry, context);
}

void platform_notify_bsp(lpaddr_t *mailbox)
{
    (void) mailbox;
}


/*
 * Return the core count
 */
size_t platform_get_core_count(void)
{
    return 0;
}

/*
 * Print system identification. MMU is NOT yet enabled.
 */
void platform_print_id(void)
{

}

/*
 * Fill out provided `struct platform_info`
 */
void platform_get_info(struct platform_info *pi)
{
    pi->arch = PI_ARCH_ARMV8A;
    pi->platform = PI_PLATFORM_QEMU;
}

void armv8_get_info(struct arch_info_armv8 *ai)
{
    (void)ai;
}

uint32_t platform_get_timer_interrupt(void){
    return 30;
}

// TODO get this right
void platform_get_dev_range(lpaddr_t* start, size_t* size){
    // the whole platform_get_dev_range is rather a hack.
    // to make the Qemu work with the approach of imx8x.

    // see: https://github.com/qemu/qemu/blob/master/hw/arm/virt.c
    *start = 0x08000000;
    *size =  0x12004000; // just include up to the uart
}
