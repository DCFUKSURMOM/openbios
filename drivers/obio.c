/*
 *   OpenBIOS Sparc OBIO driver
 *   
 *   (C) 2004 Stefan Reinauer <stepan@openbios.org>
 *   (C) 2005 Ed Schouten <ed@fxq.nl>
 * 
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "openbios/config.h"
#include "openbios/bindings.h"
#include "openbios/kernel.h"
#include "libc/byteorder.h"
#include "libc/vsprintf.h"

#include "openbios/drivers.h"
#include "openbios/nvram.h"
#include "obio.h"

#define REGISTER_NAMED_NODE( name, path )   do { \
	     bind_new_node( name##_flags_, name##_size_, \
			path, name##_m, sizeof(name##_m)/sizeof(method_t)); \
	} while(0)


/* DECLARE data structures for the nodes.  */
DECLARE_UNNAMED_NODE( ob_obio, INSTALL_OPEN, sizeof(int) );

static void
ob_new_obio_device(const char *name, const char *type)
{
    push_str("/obio");
    fword("find-device");
    fword("new-device");

    push_str(name);
    fword("device-name");

    if (type) {
        push_str(type);
        fword("device-type");
    }
}

static unsigned long
ob_reg(unsigned long base, unsigned long offset, unsigned long size, int map)
{
    PUSH(0);
    fword("encode-int");
    PUSH(offset);
    fword("encode-int");
    fword("encode+");
    PUSH(size);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    if (map) {
        unsigned long addr;

        addr = (unsigned long)map_io(base + offset, size);

        PUSH(addr);
        fword("encode-int");
        PUSH(4);
        fword("encode-int");
        fword("encode+");
        push_str("address");
        fword("property");
        return addr;
    }
    return 0;
}

static void
ob_intr(int intr)
{
    PUSH(intr);
    fword("encode-int");
    PUSH(0);
    fword("encode-int");
    fword("encode+");
    push_str("intr");
    fword("property");
}


static void
ob_zs_init(unsigned long base, unsigned long offset, int intr, int slave, int keyboard)
{
    ob_new_obio_device("zs", "serial");

    ob_reg(base, offset, ZS_REGS, 1);

    PUSH(slave);
    fword("encode-int");
    push_str("slave");
    fword("property");

    if (keyboard) {
        PUSH(-1);
        fword("encode-int");
        push_str("keyboard");
        fword("property");

        PUSH(-1);
        fword("encode-int");
        push_str("mouse");
        fword("property");
    }

    ob_intr(intr);

    fword("finish-device");
}

static char *nvram;
struct qemu_nvram_v1 nv_info;

void
arch_nvram_get(char *data)
{
    memcpy(data, nvram, NVRAM_SIZE);
}

void
arch_nvram_put(char *data)
{
    memcpy(nvram, data, NVRAM_SIZE);
}

int
arch_nvram_size(void)
{
    return NVRAM_SIZE;
}

static void
ob_nvram_init(unsigned long base, unsigned long offset)
{
    extern uint32_t kernel_image;
    extern uint32_t kernel_size;
    extern uint32_t cmdline;
    extern uint32_t cmdline_size;
    extern char boot_device;

    ob_new_obio_device("eeprom", NULL);

    nvram = (char *)ob_reg(base, offset, NVRAM_SIZE, 1);

    memcpy(&nv_info, nvram, sizeof(nv_info));

    printk("Nvram id %s, version %d\n", nv_info.id_string, nv_info.version);
    if (strcmp(nv_info.id_string, "QEMU_BIOS") || nv_info.version != 1) {
        printk("Unknown nvram, freezing!\n");
        for (;;);
    }
    kernel_image = nv_info.kernel_image;
    kernel_size = nv_info.kernel_size;
    cmdline = nv_info.cmdline;
    cmdline_size = nv_info.cmdline_size;
    boot_device = nv_info.boot_device;

    push_str("mk48t08");
    fword("model");

    fword("finish-device");

    push_str("/");
    fword("find-device");
    
    PUSH((long)&nvram[NVRAM_IDPROM]);
    PUSH(32);
    fword("encode-bytes");
    push_str("idprom");
    fword("property");
}

static void
ob_fd_init(unsigned long base, unsigned long offset, int intr)
{
    ob_new_obio_device("SUNW,fdtwo", "block");

    ob_reg(base, offset, FD_REGS, 0);

    ob_intr(intr);

    fword("finish-device");
}


static void
ob_sconfig_init(unsigned long base, unsigned long offset)
{
    ob_new_obio_device("slavioconfig", NULL);

    ob_reg(base, offset, SCONFIG_REGS, 0);

    fword("finish-device");
}

static void
ob_auxio_init(unsigned long base, unsigned long offset)
{
    ob_new_obio_device("auxio", NULL);

    ob_reg(base, offset, AUXIO_REGS, 0);

    fword("finish-device");
}

static void
ob_power_init(unsigned long base, unsigned long offset, int intr)
{
    ob_new_obio_device("power", NULL);

    ob_reg(base, offset, POWER_REGS, 0);

    ob_intr(intr);

    fword("finish-device");
}

static void
ob_counter_init(unsigned long base, unsigned long offset)
{
    volatile struct sun4m_timer_regs *regs;

    ob_new_obio_device("counter", NULL);

    PUSH(0);
    fword("encode-int");
    PUSH(offset);
    fword("encode-int");
    fword("encode+");
    PUSH(COUNTER_REGS);
    fword("encode-int");
    fword("encode+");
    PUSH(0);
    fword("encode-int");
    fword("encode+");
    PUSH(offset + 0x10000);
    fword("encode-int");
    fword("encode+");
    PUSH(COUNTER_REGS);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    fword("finish-device");

    regs = map_io(base + offset, sizeof(*regs));
    regs->l10_timer_limit = (((1000000/100) + 1) << 10);
    regs->cpu_timers[0].l14_timer_limit = 0;
}

static void
ob_interrupt_init(unsigned long base, unsigned long offset)
{
    volatile struct sun4m_intregs *regs;

    ob_new_obio_device("interrupt", NULL);

    PUSH(0);
    fword("encode-int");
    PUSH(offset);
    fword("encode-int");
    fword("encode+");
    PUSH(INTERRUPT_REGS);
    fword("encode-int");
    fword("encode+");
    PUSH(0);
    fword("encode-int");
    fword("encode+");
    PUSH(offset + 0x10000);
    fword("encode-int");
    fword("encode+");
    PUSH(INTERRUPT_REGS);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    regs = map_io(base + offset, sizeof(*regs));
    regs->set = ~SUN4M_INT_MASKALL;
    regs->cpu_intregs[0].clear = ~0x17fff;
    
    // is this really correct?
    PUSH(regs);
    fword("encode-int");
    PUSH(regs);
    fword("encode-int");
    fword("encode+");
    push_str("address");
    fword("property");

    fword("finish-device");

}


static void
ob_obio_open(__attribute__((unused))int *idx)
{
	int ret=1;
	RET ( -ret );
}

static void
ob_obio_close(__attribute__((unused))int *idx)
{
	selfword("close-deblocker");
}

static void
ob_obio_initialize(__attribute__((unused))int *idx)
{
    push_str("/");
    fword("find-device");
    fword("new-device");

    push_str("obio");
    fword("device-name");

    push_str("hierarchical");
    fword("device-type");

    PUSH(2);
    fword("encode-int");
    push_str("#address-cells");
    fword("property");

    PUSH(1);
    fword("encode-int");
    push_str("#size-cells");
    fword("property");

    fword("finish-device");
}

static void
ob_set_obio_ranges(unsigned long base)
{
    push_str("/obio");
    fword("find-device");
    PUSH(0);
    fword("encode-int");
    PUSH(0);
    fword("encode-int");
    fword("encode+");
    PUSH(0);
    fword("encode-int");
    fword("encode+");
    PUSH(base);
    fword("encode-int");
    fword("encode+");
    PUSH(SLAVIO_SIZE);
    fword("encode-int");
    fword("encode+");
    push_str("ranges");
    fword("property");
}

static void
ob_obio_decodeunit(__attribute__((unused)) int *idx)
{
    fword("decode-unit-sbus");
}


static void
ob_obio_encodeunit(__attribute__((unused)) int *idx)
{
    fword("encode-unit-sbus");
}

NODE_METHODS(ob_obio) = {
	{ NULL,			ob_obio_initialize	},
	{ "open",		ob_obio_open		},
	{ "close",		ob_obio_close		},
	{ "encode-unit",	ob_obio_encodeunit	},
	{ "decode-unit",	ob_obio_decodeunit	},
};


int
ob_obio_init(unsigned long slavio_base)
{

    //printk("Initializing OBIO devices...\n");
#if 0 // XXX
    REGISTER_NAMED_NODE(ob_obio, "/obio");
    device_end();
    ob_set_obio_ranges(slavio_base);
#endif

    // Must be before zs@0,0 or Linux won't boot
    ob_zs_init(slavio_base, SLAVIO_ZS1, ZS_INTR, 0, 0);

    ob_zs_init(slavio_base, SLAVIO_ZS, ZS_INTR, 1, 1);

    ob_nvram_init(slavio_base, SLAVIO_NVRAM);

    ob_fd_init(slavio_base, SLAVIO_FD, FD_INTR);

    ob_sconfig_init(slavio_base, SLAVIO_SCONFIG);

    ob_auxio_init(slavio_base, SLAVIO_AUXIO);

    ob_power_init(slavio_base, SLAVIO_POWER, POWER_INTR);

    ob_counter_init(slavio_base, SLAVIO_COUNTER);

    ob_interrupt_init(slavio_base, SLAVIO_INTERRUPT);

    return 0;
}