/*
 * QEMU DEC 21143 (Tulip) emulation
 *
 * Copyright (C) 2011, 2013 Antony Pavlov
 * Copyright (C) 2013 Dmitry Smagin
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/pci/pci.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

#include "hw/nvram/eeprom93xx.h"
#include "tulip.h"
#include "tulip_mdio.h"

#define TYPE_PCI_TULIP "tulip"

#define PCI_TULIP(obj) \
    OBJECT_CHECK(PCITulipState, (obj), TYPE_PCI_TULIP)

static const VMStateDescription vmstate_tulip = {
    .name = "tulip",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent, TulipState),
        VMSTATE_MACADDR(conf.macaddr, TulipState),
        /* FIXME: add VMSTATE_STRUCT for saving state */
        VMSTATE_END_OF_LIST()
    }
};

/* PCI Interfaces */

static const MemoryRegionOps tulip_mmio_ops = {
    .read = tulip_csr_read,
    .write = tulip_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pci_physical_memory_write(void *dma_opaque, hwaddr addr,
                                      uint8_t *buf, int len)
{
    pci_dma_write(dma_opaque, addr, buf, len);
}

static void pci_physical_memory_read(void *dma_opaque, hwaddr addr,
                                     uint8_t *buf, int len)
{
   pci_dma_read(dma_opaque, addr, buf, len);
}

static void pci_tulip_cleanup(NetClientState *nc)
{
    TulipState *s = qemu_get_nic_opaque(nc);

    tulip_cleanup(s);
}

static void pci_tulip_uninit(PCIDevice *dev)
{
    TulipState *d = Tulip(dev);

    qemu_free_irq(d->irq);
    //memory_region_destroy(&d->state.mmio);
    //memory_region_destroy(&d->io_bar);
    timer_del(d->timer);
    timer_free(d->timer);
    eeprom93xx_free(&dev->qdev, d->eeprom);
    qemu_del_nic(d->nic);
}

NetClientInfo net_tulip_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = tulip_can_receive,
    .receive = tulip_receive,
    .cleanup = pci_tulip_cleanup,
    .link_status_changed = tulip_set_link_status,
};

void pci_tulip_realize(PCIDevice *pci_dev, Error ** e)
{
    DeviceState *d = DEVICE(pci_dev);
    TulipState *s = Tulip(pci_dev);
    uint8_t *pci_conf;

    pci_conf = pci_dev->config;

    /* TODO: RST# value should be 0, PCI spec 6.2.4 */
    pci_conf[PCI_CACHE_LINE_SIZE] = 0x10;

    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */

    /* PCI interface */
    memory_region_init_io(&s->mmio, OBJECT(d), &tulip_mmio_ops, s,
                          "tulip-mmio", TULIP_CSR_REGION_SIZE);
    memory_region_init_io(&s->io_bar, OBJECT(d), &tulip_mmio_ops, s,
                          "tulip-io", TULIP_CSR_REGION_SIZE);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    s->irq = pci_allocate_irq(pci_dev);

    s->phys_mem_read = pci_physical_memory_read;
    s->phys_mem_write = pci_physical_memory_write;
    s->dma_opaque = pci_dev;

    /* FIXME: Move everything below to this func: */
    tulip_init(pci_dev, e);
}

static void pci_tulip_reset(DeviceState *dev)
{
    TulipState *d = Tulip(dev);

    tulip_reset(d);
}

static Property tulip_properties[] = {
    DEFINE_NIC_PROPERTIES(TulipState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void tulip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_tulip_realize;
    k->exit = pci_tulip_uninit;
    k->vendor_id = PCI_VENDOR_ID_DEC;
    k->device_id = PCI_DEVICE_ID_DEC_21142;
    k->revision = 0x41; /* 21143 chip */
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->desc = "DEC 21143 Tulip";
    dc->reset = pci_tulip_reset;
    dc->vmsd = &vmstate_tulip;
    dc->props = tulip_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

const TypeInfo tulip_info = {
    .name          = TYPE_PCI_TULIP,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TulipState),
    .class_init    = tulip_class_init,
};
