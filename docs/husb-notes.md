# USB0 host mode on the AC791N / WL82 (polled, bare metal)

Register-level notes for a small host driver on USB0 (the full-speed MUSB-derived core at 0x11800).
Target: JieLi mask ROM in download mode (MSC bulk-only, VID 4C4A PID 8057, FS).

## Sources and how they are cited

- `usb.c.o` from `cpu/wl82/liba/cpu.a` (SDK file `lib/driver/cpu/wl82/usb.c`). Cited as
  **`usb.dis 0xNNNN`** (offset in its disassembly: the bitcode compiled with the pi32v2 clang at -O1, then objdump). The same bitcode run through
  `llvm-dis` keeps function names, argument names (`host_ep`,
  `target_ep`, `xfer`, `kstart`, `reset_delay`) and struct field names (`EP0_CNT`, `EP1_TADR`...).
  It reads much more clearly than the -O1 disassembly. Facts below were checked in both.
- `usb_phy.c.o` (`lib/driver/device/usb/usb_phy.c`, has debug info): **`usb_phy.dis 0xNN`**
  / `usb_phy.c:line`.
- `otg.c.o` and `clock.c.o` from the same `cpu.a`, also run through `llvm-dis` .
- SDK sources: `SDK/` = the JieLi AC79 SDK (fw-AC79_AIoT_SDK, Apache-2.0).
- Device-mode driver: `fm1_usb.h`, `usb.c` = felucca-bsp's `chip/` in [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0).
- **INFERRED** = not read in any of these. It comes from MUSB convention or from reasoning, and
  should be checked on hardware.

---

## 1. Register map

### 1.1 Memory-mapped registers (USB0)

| Addr | Name (WL82.h `JL_USB_TypeDef`) | Use in host mode |
|---|---|---|
| 0x11800 | CON0 | control/status, bits below |
| 0x11804 | CON1 | SIE indirect access window |
| 0x11808 | EP0_CNT | EP0 TX byte count (written before CSR0 TxPktRdy) |
| 0x1180C/10/14 | EP1/2/3_CNT | EPn TX byte count |
| 0x11818 | EP0_ADR | EP0 DMA buffer, shared by TX and RX |
| 0x1181C + 8(n-1) | EPn_TADR (n=1..3) | TX DMA buffer |
| 0x11820 + 8(n-1) | EPn_RADR (n=1..3) | RX DMA buffer |
| 0x11834/38/3C | EP4_CNT/TADR/RADR | EP4 |
| 0x51000 | USB_IO CON0 | pad control |
| 0x51004 | USB_IO CON1 | pad input read (bit0 DP, bit1 DM); written 0 by host init |
| 0x10010 | CLK_CON1 | USB clock gate (bits 1:0) |

Sources: `SDK/include_lib/driver/cpu/wl82/asm/WL82.h:465-485`. The EP_CNT address is
`usb_write_ep_cnt` (usb.dis 0x2f4: `0x11808 + 4*ep`, ep 4 → 0x11834). TADR/RADR/EP0_ADR are
`usb_set_dma_taddr` 0x454 and `usb_set_dma_raddr` 0x4bc (IR: `EP0_ADR`, `EP1_TADR + 2*(ep-1)`
words, `EP1_RADR + 2*(ep-1)`, `EP4_TADR/RADR`). They match `fm1_usb.h:25-35`.
`usb_write_ep_cnt` (USB0) writes the length only. Bit 31 and the size<<16 field are USB1-only.

### 1.2 CON0 (0x11800) bits

Names from `SDK/include_lib/driver/cpu/wl82/asm/usb.h:24-40`:

| bit | name | evidence / host use |
|---|---|---|
| 0 | PHY_ON | host init sets (`usb_h_sie_init` 0x1ec6 `|= 0x25`); `usb_phy_status` 0x1a2 reads it |
| 1 | LOW_SPEED | `usb_set_low_speed` 0x378; set by chirp_and_reset when DEVCTL.LSDev |
| 2 | USB_NRST | SIE out of reset. `musb_read_usb`/`musb_write_usb` return without access if 0 (0x72, 0xee); `usb_h_sie_reset` 0x20e2 pulses it |
| 3 | TM1 | device init sets it (0x3C), host init does not |
| 4 | CID | ID pin. Device init sets it (0x3C); host leaves it 0 (A-device / host). INFERRED meaning from the name |
| 5 | VBUS | set in both modes (host 0x25, device 0x3C). INFERRED: "VBUS valid" to the core; it does not power anything |
| 9 | PDCHKDP | `usb_hotplug_enable` mode&2 (0x21a2) |
| 10 | SOFIE | `usb_sofie_enable` 0x20e |
| 11 | SIEIE | SIE interrupt enable (`usb_sie_enable` 0x1de); leave 0 for polling |
| 12 | CLR_SOF_PND | write 1 (`usb_sof_clr_pnd` 0x22e) |
| 13 | SOF_PND | `usb_read_sofpnd` 0x186 |
| 14 | SIE_PND | |
| 15 | CHKDPO | `usb_check_dpo` 0x126 |
| 16 | DM_SE | D- line level (`usb_read_dm_se` 0x172) |
| 17 | DP_SE | D+ line level (`usb_read_dp_se` 0x156) |
| 19+n | EPn disable | `usb_enable_ep` 0x11a8 clears `1<<(19+ep)`. Host SDK never touches it; 0 after CON0=0 |

### 1.3 CON1 (0x11804): SIE byte access

Write (`musb_write_usb` usb.dis 0x9e-0x116):
`CON1 = ((reg<<8) & 0xFFFF00) | (val & 0xFF); csync;` then poll until `CON1 & 0x8000` (MACK, bit 15).
The SDK polls at most 20001 times and then gives up silently.
Read (`musb_read_usb` 0x22-0x9c): `CON1 = ((reg<<8) & 0xFFBF00) | 0x4000` (MC_RNW, bit 14); `csync`;
poll bit 15; data = `CON1 & 0xFF`. Both return immediately if `CON0 & 4` (USB_NRST) is 0.
Values are truncated to 8 bits (0xfc `r2 = r4.b0`). So the SDK's `CSR0H_DISPING` (bit 11) and
`TXMAXP = 1023` reach USB0 as 0x00 / 0xFF. Same scheme as `fm1_usb.h:68-79`.
The device driver saw the SIE never answer when powered without a cable (`usb.c:1622-1624`),
which is why it needs this timeout.

### 1.4 SIE (MUSB) byte registers on USB0

Indices from `SDK/include_lib/driver/device/usb/usb_phy.h:61-90`. Each was confirmed in the
library accessor:

| idx | name | accessor (usb.dis) | host-mode use |
|---|---|---|---|
| 0x00 | FADDR | `usb_write_faddr` 0x6ea | **target device address** (see 1.6) |
| 0x01 | POWER | `usb_write_power` 0x5c2 / read 0x5e6 | bit1 Suspend, bit2 Resume, bit3 Reset, bit4 HSMode (USB1), 0x60 written at init |
| 0x02/03 | INTRTX1/2 | `usb_read_intr` 0x786 | TX-done/err per EP (bit0 = EP0) |
| 0x04/05 | INTRRX1/2 | same | |
| 0x06 | INTRUSB | same | bits `usb_phy.h:94-101`: 0 Suspend, 1 Resume, 2 Reset/Babble, 3 SOF, 4 Connect, 5 Disconnect, 6 SessReq, 7 VbusErr |
| 0x07/08 | INTRTX1E/2E | `usb_set/clr_intr_txe` 0x87e/0x8d2 | 0 for polling |
| 0x09/0A | INTRRX1E/2E | `usb_set/clr_intr_rxe` 0x93a/0x98e | 0 |
| 0x0B | INTRUSBE | `usb_write_intr_usbe` 0x858 | 0 |
| 0x0C/0D | FRAME1/2 | `musb_read_sofframe` 0x6aa | |
| 0x0E | INDEX | `musb_write_index` 0x24e | selects EP for 0x10-0x1B |
| 0x0F | DEVCTL | `usb_read/write_devctl` 0x60a/0x62e | bit0 Session, bit1 HostReq (written 3), bit2 HostMode, bit5 LSDev, bit6 FSDev |
| 0x10 | TXMAXP | `usb_write_txmaxp` 0xae6 | SDK writes 0xFF |
| 0x11 | CSR0 (INDEX 0) / TXCSR1 | `usb_read/write_csr0` 0x652/0x67e; `usb_write_txcsr` 0x9f6 | |
| 0x12 | TXCSR2 | `usb_write_txcsr` writes `csr>>8` here | |
| 0x13 | RXMAXP | `usb_write_rxmaxp` 0xab2 | 0xFF |
| 0x14 | RXCSR1 | `usb_write/read_rxcsr` 0xa34/0xa72 | |
| 0x15 | RXCSR2 | same | |
| 0x16 | COUNT0 (INDEX 0) / RXCOUNT1 | `usb_read_count0` 0x75c, `usb_read_rxcount` 0xbee | received length |
| 0x17 | RXCOUNT2 | `usb_read_rxcount` | |
| 0x18 | TXTYPE | `usb_write_txtype` 0xb1a | `(xfer<<4) | target_ep` |
| 0x19 | TXINTERVAL | `usb_write_txinterval` 0xb82 (writes INDEX then 0x19) | NAK limit / interval |
| 0x1A | RXTYPE | `usb_write_rxtype` 0xb4e | `(xfer<<4) | target_ep` |
| 0x1B | RXINTERVAL | `usb_write_rxinterval` 0xbb8 | |

DEVCTL bit meanings: `usb_h_chirp_and_reset` (IR) treats bit2==0 as "exit host mode", bit6 as
"full speed" and bit5 as "low speed" (log strings `.str.17/.18/.19`). POWER bits:
`usb_h_chirp_and_reset` (Reset = 0x08, HSMode = 0x10 on USB1), `usb_h_entry_suspend`
(usb_phy.c:117, sets 0x02) and `usb_h_resume` (usb_phy.c:126, 0x04 for 20 ms).

### 1.5 CSR bits, host mode

From `usb_phy.h:138-167`. Each column shows how the library uses it.

**CSR0 (0x11, INDEX 0), host**

| bit | name | use |
|---|---|---|
| 0x01 | RxPktRdy | IN packet in EP0 buffer (usb_ctrl_transfer.c:133) |
| 0x02 | TxPktRdy | start OUT/SETUP; HW clears when ACKed (ctrl.c:150) |
| 0x04 | RxStall | STALL received (ctrl.c:120) |
| 0x08 | SetupPkt | with TxPktRdy: send SETUP (ctrl.c:59) |
| 0x10 | Error | 3 failed tries; SDK writes CSR0=0 (ctrl.c:125-129) |
| 0x20 | ReqPkt | request one IN packet (ctrl.c:63,143) |
| 0x40 | StatusPkt | with ReqPkt or TxPktRdy: status stage (ctrl.c:65,76) |
| 0x80 | NAK timeout | INFERRED (MUSB); not in `usb_phy.h`, never checked by SDK |

**TXCSR1 (0x11, INDEX n), host**

| bit | name | use |
|---|---|---|
| 0x01 | TxPktRdy | `usb_h_ep_write` writes 1 (0x165c); busy while set (0x16c8) |
| 0x02 | FIFONotEmpty | |
| 0x04 | Error | clear, re-send packet (0x16c4/0x16ea) |
| 0x08 | FlushFIFO | ep_config writes 0x48 |
| 0x20 | RxStall | clear, return -16 (0x16c0/0x1724) |
| 0x40 | ClrDataTog | ep_config writes 0x48 → toggle = DATA0 |
| 0x80 | NAK timeout ("TXCSRH_NAK") | clear, re-send same packet (0x16bc/0x16dc) |

**RXCSR1 (0x14, INDEX n), host**

| bit | name | use |
|---|---|---|
| 0x01 | RxPktRdy | `usb0_h_ep_read` 0x1a2c |
| 0x02 | FIFOFull | |
| 0x04 | Error | log "Error Request In again", write 0x80, -6 (0x1a38/0x1abc) |
| 0x08 | DataError / NAK timeout | log "DataError", write 0x80, -11 (0x1a34/0x1aa6) |
| 0x10 | FlushFIFO | **written alone (0x10) to release a received packet** (0x1a70/0x1ae8) |
| 0x20 | ReqPkt | request one IN packet (0x19ac, 0x1b0a) |
| 0x40 | RxStall | clear, -16 (0x1a30/0x1a94) |
| 0x80 | ClrDataTog | ep_config writes 0x90 |

RXCSR2 bit0 = IncompRx (`RXCSRH_IncompRx BIT(8)`) and bit4 = PIDError (`BIT(12)`); bit6 = ISO
(peripheral, `BIT(14)`). TXCSR2 bit5 = Mode/DIR (`BIT(13)`) and bit6 = ISO (`BIT(14)`).
ep_config writes both CSR2 bytes as 0 (`usb_write_txcsr/rxcsr` write the high byte to 0x12/0x15).
The device driver releases RX the same way: `csr = (csr & ~0x164) | 0x10` (`usb.c:1017-1019`).

**Data toggle.** The only toggle control the library uses is ClrDataTog: TXCSR1 0x40 and
RXCSR1 0x80. It writes these in `usb_h_ep_config` (0x1320 TX 0x48, 0x13a4 RX 0x90) and, for RX,
after Error/DataError. No DataToggle / DataToggleWriteEnable bits appear anywhere in usb.c.o
(no constant 0x100/0x200/0x400 is written to a CSR). INFERRED from the MUSB MHDRC layout, which
JieLi follows for IncompRx (bit 8), PIDError (bit 12), ISO (bit 14) and DISPING (CSR0 bit 11):

- TX: TXCSR2 bit0 = DataToggle, bit1 = DataToggleWrEnable.
- RX: RXCSR2 bit1 = DataToggle, bit2 = DataToggleWrEnable.
- Not needed for your case. After CLEAR_FEATURE(HALT) the toggle must be DATA0, which
  ClrDataTog gives directly (see 5.4).

### 1.6 Function address, target endpoint, type

- `usb_write_txfuncaddr` / `usb_write_rxfuncaddr` (0xc50 / 0xc64) do something only for
  `id == 1` (USB1, per-EP func-addr regs at 0x16080 + 8*ep). On USB0 they do nothing.
  So **USB0 has one target address: FADDR (SIE 0x00)**. `usb_ctlXfer` writes it before every
  control stage (`usb_ctrl_transfer.c:53`). Bulk transfers rely on the value left there.
  INFERRED: FADDR = target address in host mode (MUSB FS host convention); consistent with the
  above.
- TXTYPE/RXTYPE = `(xfer << 4) | target_ep`, with xfer as in `ch9.h:404-407` (1 ISO, 2 BULK,
  3 INT). See `usb_h_ep_write` IR (`%or = shl xfer,4 | target_ep`, written at 0x1648) and
  `usb0_h_ep_read` 0x1998-0x19a6. Bulk = `0x20 | ep`. USB0 has no speed field (USB1 adds
  `speed_table[]<<6`, IR `if.else27`); USB0 low speed is CON0 bit1.
- TXMAXP/RXMAXP: the SDK writes 1023, so 0xFF reaches USB0 (`usb_h_ep_config` 0x1310/0x1394).
  The device driver also uses 0xFF (`usb.c:597`). Software splits transfers into 64-byte
  packets: `txmap/rxmap = 0x40` for usb_id 0 (`usb_bulk_transfer.c:103-107, 232-236`).
- TXINTERVAL/RXINTERVAL are written only for xfer 1 (ISO) and 3 (INT) (`usb_h_ep_config` IR
  switch). **Bulk and EP0 never get a NAK limit**, so they stay at the post-reset value.
  INFERRED (MUSB): 0 means NAK timeout disabled, and the core retries NAKed tokens indefinitely.

### 1.7 USB_IO CON0 (0x51000)

Bit names from `asm/usb.h:46-61`, confirmed via `usb_set_*` in gpio.c.o:

| bit | name |
|---|---|
| 0 / 1 | DPOUT / DMOUT |
| 2 / 3 | DPIE / DMIE (direction, 1 = input) |
| 4 / 5 | DPPD / DMPD (pull-down) |
| 6 / 7 | DPPU / DMPU (pull-up) |
| 8 | IO_PU_MODE |
| 9 / 10 | DPDIE / DMDIE (digital input enable) |
| 11 | IO_MODE |
| 12 | SR |
| 13 / 14 | DPDIEH / DMDIEH |

`usb_iomode(0, en)` sets/clears bits 8+11 (0x900) together: set = GPIO, clear = USB. IO numbers:
DP = 148, DM = 149 (`asm/gpio.h:186-187`). `usb_set_die` uses shift `gpio-139`, pull-down
`gpio-144`, pull-up `gpio-142`.

### 1.8 CLK_CON1 (0x10010)

- Bits 1:0: `usb_h_entry_suspend` sets 3 (usb_phy.dis 0x134); `usb_h_resume` clears them
  (0x156); the device driver clears them ("USB clock = PLL48M", `fm1_usb.h:40`).
- `clk_early_init` (clock.c.o IR) does `CLK_CON1 = (CLK_CON1 & ~0xC00) | 0x400`, records
  48 MHz as the USB clock, and clears bit 16. INFERRED: bits 11:10 are a USB clock mux.
  The ROM already ran USB0 in device mode, so the 48 MHz source should be up.
- `usb_pll_init` (0x16A00) is the USB1 / HS PLL, not needed for USB0.

---

## 2. (a) Host-mode init

Helpers (same as the device driver):

```c
#define CON0 (*(volatile u32 *)0x11800)
#define CON1 (*(volatile u32 *)0x11804)
#define EP_CNT(n)  (*(volatile u32 *)(0x11808 + 4*(n)))
#define EP0_ADR    (*(volatile u32 *)0x11818)
#define EP_TADR(n) (*(volatile u32 *)(0x1181C + 8*((n)-1)))
#define EP_RADR(n) (*(volatile u32 *)(0x11820 + 8*((n)-1)))
#define IO_CON0 (*(volatile u32 *)0x51000)
#define IO_CON1 (*(volatile u32 *)0x51004)
#define CLK_CON1 (*(volatile u32 *)0x10010)
enum { FADDR=0, POWER=1, INTRTX1=2, INTRTX2=3, INTRRX1=4, INTRRX2=5, INTRUSB=6, INTRTX1E=7,
       INTRTX2E=8, INTRRX1E=9, INTRRX2E=10, INTRUSBE=11, FRAME1=12, FRAME2=13, INDEX=14,
       DEVCTL=15, TXMAXP=16, CSR0=17, TXCSR1=17, TXCSR2=18, RXMAXP=19, RXCSR1=20, RXCSR2=21,
       COUNT0=22, RXCOUNT1=22, RXCOUNT2=23, TXTYPE=24, TXINTERVAL=25, RXTYPE=26, RXINTERVAL=27 };
int  sie_wr(u32 r, u32 v) { CON1 = ((r<<8)&0xFFFF00)|(v&0xFF); csync(); /* wait CON1 & 0x8000, <=20001 polls */ }
u32  sie_rd(u32 r)        { CON1 = ((r<<8)&0xFFBF00)|0x4000;   csync(); /* wait CON1 & 0x8000 */ return CON1 & 0xFF; }
```

Sequence. Each step follows the SDK; the source is on the right.

```c
/* 0. start state (yours): controller off, SIE in reset */
CON0 = 0; CON1 = 0;                 /* fm1_usb_reset(); SIE reset = CON0.bit2 low (usb_h_sie_reset 0x20e2) */
IO_CON0 = 0x0C;                     /* optional: pads as inputs, as usb_sie_close 0x2286 / usb_hotplug_disable */
delay_ms(20);                       /* fm1_usb_reset comment: wait >= 20 ms; usb_h_sie_reset uses delay(10000) */

/* 1. clock */
CLK_CON1 &= ~3u;                    /* usb_h_resume (usb_phy.c:126); fm1_usb_attach */

/* 2. usb_h_sie_init(0)  (usb.dis 0x1e62-0x1ece, IR usb_h_sie_init if.then) */
IO_CON0 = 0;                        /* JL_USB_IO->CON0 = 0 */
IO_CON1 = 0;                        /* JL_USB_IO->CON1 = 0 */
delay(1000);                        /* SDK busy-loop count; INFERRED: >= ~1 ms is safe */
IO_CON0 |= (1u<<10) | (1u<<9);      /* gpio_set_die(DM,1), gpio_set_die(DP,1) */
IO_CON0 &= ~((1u<<7) | (1u<<6));    /* gpio_set_pull_up(DM/DP, 0) */
IO_CON0 |= (1u<<5) | (1u<<4);       /* gpio_set_pull_down(DM/DP, 1): host 15k pull-downs */
IO_CON0 &= ~(1u<<8);                /* IO_PU_MODE off */
IO_CON0 &= ~(1u<<11);               /* IO_MODE off -> pads belong to the USB PHY */
CON0 |= 0x25;                       /* PHY_ON | USB_NRST | VBUS; CID(bit4)=0, TM1(bit3)=0, LOW_SPEED=0 */
delay(1000);
/* IO_CON0 is now 0x0630. Device mode instead sets SR(bit12), DP pull-up(bit6), CON0 |= 0x3C */

/* 3. EP DMA buffers (CON0.bit2 must be set before any SIE access; DMA regs can be written any time) */
EP0_ADR = (u32)ep0buf;              /* usb_host.c:451-454 usb_set_dma_taddr(id,0,..) -> EP0_ADR */

/* 4. _usb_host_mount (usb_host.c:437-445) + usb_host_init (usb_phy.c:143, usb_phy.dis 0x196) */
sie_wr(POWER, 0x60);                /* FUSB_MODE==0 (asm/usb.h:19) -> 0x60; usb_host.c:441. Meaning of bits 5/6 on USB0: open */
sie_wr(INTRUSBE, 0);                /* usb_write_intr_usbe(id,0) */
sie_wr(INTRTX1E, 0); sie_wr(INTRTX2E, 0);   /* usb_clr_intr_txe(id,-1) 0x8fc-0x930 */
sie_wr(INTRRX1E, 0); sie_wr(INTRRX2E, 0);   /* usb_clr_intr_rxe(id,-1) */
/* then usb_h_chirp_and_reset(): sections 3-4 */
```

Do not set CON0.SIEIE (bit 11, `usb_sie_enable`, usb_host.c:456) for polling. The SDK also
registers an IRQ (`usb_h_isr_reg`) and runs its bulk path from interrupts. All register work is
the same; you replace the ISR with polling.

Teardown: `usb_sie_close(0)` (0x2260): `CON0 = 0; CON1 = 0; IO_CON0 = 0x0C`.

---

## 3. (b) Attach and speed detection

There are two levels.

1. **Line state, before the Session.** This is what the OTG hotplug code uses (otg.c.o IR
   `usb_hotplug_detect`). With PHY_ON and the 15k pull-downs on, read CON0 bit17 DP_SE and bit16
   DM_SE. DP high means an FS device (D+ pull-up); DM high means LS. `usb_h_dev_status`
   (0x35c) returns 1 ("nothing there", SE0) when both are 0. The SDK uses it during transfers to
   count consecutive SE0 samples (offline if > 5 per timeout tick on bulk, or > 1000 polls on
   control).

   ```c
   while (!(CON0 & (1u<<17)))  ;     /* wait D+ high = FS device attached; debounce yourself (INFERRED: >=100 ms per USB spec) */
   ```

2. **Start the session and read the speed** (`usb_h_chirp_and_reset`, USB0 branch, usb.dis
   0x1f06-0x2096, IR `while.cond`):

   ```c
   sie_wr(DEVCTL, 0x03);                   /* Session | HostReq */
   deadline = now + timeout;               /* SDK: MOUNT_TIMEOUT = 50 jiffies = 500 ms (usb_common_def.h:85) */
   for (;;) {
       d = sie_rd(DEVCTL);
       if (now > deadline)          return -6;   /* "time out %x" */
       if (!(d & 0x04))             return -10;  /* HostMode lost -> "exit host mode" (checked on every pass, incl. the first) */
       /* also -10 if usb_h_dev_status()==1 for > 1000 consecutive passes */
       if (d & 0x40) { speed = FS; break; }      /* FSDev */
       if (d & 0x20) { speed = LS; CON0 |= 2; break; } /* LSDev -> usb_set_low_speed(0,1) */
       usb_mdelay(1);                            /* = 1 OS tick = 10 ms (usb_mdelay rounds to 10 ms ticks, usb_phy.c:29) */
   }
   ```

   HostMode must already read as 1 on the first pass after writing 3. INFERRED: CID=0 makes the
   core an A-device, so it becomes host as soon as Session is set.

---

## 4. (c) Bus reset and post-reset timing

`usb_h_chirp_and_reset` (USB0), after speed detection (IR `do.body` / `do.end`):

```c
deadline = now + timeout;
do {                                         /* at least two passes: p is read before the write */
    p = sie_rd(POWER);
    sie_wr(POWER, (p & 0xF7) | 0x08);         /* POWER.Reset = 1 */
    if (now > deadline) return 1;            /* "time out" */
} while (!(p & 0x08));
usb_mdelay(reset_delay);                     /* SE0 held: MOUNT_RESET = 40 ms (usb_common_def.h:84), +10 ms per retry (usb_host.c:446-447), 3 tries (MOUNT_RETRY) */
sie_wr(POWER, sie_rd(POWER) & 0xF7);         /* end reset */
```

Then, in `_usb_host_mount` (usb_host.c:451-476):

| step | delay |
|---|---|
| EP0_ADR = ep0 buffer | |
| wait before first request | `usb_mdelay(reset_delay)` = 40 ms again (usb_host.c:457) |
| GET_DESCRIPTOR(device, 18), FADDR = 0, EP0 mps assumed 8 | result ignored (usb_host.c:461-465) |
| wait before SET_ADDRESS | 20 ms (usb_host.c:468) |
| SET_ADDRESS(rand()%16 + 1) | (usb_host.c:469-472) |
| wait after SET_ADDRESS | 20 ms, then GET_DESCRIPTOR again with the new address (usb_host.c:475-478) |
| then | bMaxPacketSize0 is used; config descriptor, SET_CONFIGURATION |

INFERRED: SOFs start automatically once the reset ends. Check by watching CON0 bit13 SOF_PND
(clear with `CON0 |= 1<<12`), or FRAME1/2 counting.

---

## 5. (d) Control transfers on EP0

`usb_ctlXfer` / `usb_control_transfers`, `SDK/apps/common/usb/host/usb_ctrl_transfer.c:44-218`.
USB0 specifics come from the accessors:

- `usb_write_csr0` (0x67e) writes INDEX=0 then SIE 0x11, 8 bits only. `usb_read_csr0` (0x652)
  reads the same way.
- `usb_write_ep0` (0x38e; IR) does `memcpy(ep0_dma_buf, src, len); EP0_CNT = len`.
  `usb_read_ep0` does `memcpy(dst, ep0_dma_buf, len)`. EP0_ADR is one buffer for both directions.
- `usb_read_count0` (0x75c) reads INDEX=0 then SIE 0x16.
- The EP0 buffer is 64 + 4 bytes, 4-aligned (`usb_host_config.c:158`). `usb_epbuf_manager.c:46`
  says USB0 RX writes 4 extra bytes: "usb0 rx needs an extra 4 bytes to store the received CRC".

```c
/* every stage */
sie_wr(FADDR, devaddr);                                /* ctrl.c:53 */

/* SETUP (ctrl.c:57-59) */
memcpy(ep0buf, setup8, 8); csync(); EP_CNT(0) = 8;
sie_wr(INDEX, 0); sie_wr(CSR0, 0x0A);                  /* SetupPkt | TxPktRdy */
wait: c = rd_csr0();  if (c & 0x04) STALL;  if (c & 0x10) { wr_csr0(0); ERROR; }
      done when !(c & 0x02)                              /* ctrl.c:119-153 */

/* DATA IN, all packets in one loop (ctrl.c:61-63, 131-148) */
wr_csr0(0x20);                                         /* ReqPkt */
loop: c = rd_csr0(); STALL/ERROR as above;
      if (c & 0x01) {
          n = sie_rd(COUNT0); n = min(n, left);
          ssync(); memcpy(dst, ep0buf, n);  dst += n; left -= n;   /* SDK has no ssync on USB0; device driver does */
          if (n < mps0) left = 0;                      /* short packet ends the stage */
          if (left) wr_csr0(0x20);                     /* clears RxPktRdy, requests the next packet */
          else    { wr_csr0(0x00); break; }            /* clears RxPktRdy (SDK writes DISPING, truncated to 0) */
      }

/* DATA OUT, one packet per call, repeated while wLength (ctrl.c:68-74, 199-207) */
n = min(left, mps0); memcpy(ep0buf, src, n); csync(); EP_CNT(0) = n;
wr_csr0(0x02);  wait !(TxPktRdy), STALL/ERROR as above

/* STATUS: direction opposite to the data stage, IN if no data (ctrl.c:184-211) */
/* status OUT (after IN data): */
wr_csr0(0x42);                 /* StatusPkt | TxPktRdy; wait !(c & 0x02) */
/* status IN (after OUT data, or no data stage): */
wr_csr0(0x60);                 /* StatusPkt | ReqPkt; wait c & 0x01 (COUNT0 = 0), then wr_csr0(0x00) */
```

- **Completion by polling:** only CSR0 is polled. TxPktRdy cleared means a SETUP/OUT/status-OUT
  finished; RxPktRdy set means an IN packet arrived. The SDK's EP0 interrupt (INTRTX bit0) is
  only a wake-up for the semaphore (`ep0_h_isr`, ctrl.c:31-35, 83-87). Not needed when polling.
- **Errors:** RxStall (0x04) gives `-DEV_ERR_CONTROL_STALL`. The SDK does not clear it; the next
  CSR0 write (0x0A on the next SETUP) writes 0 there. INFERRED: write CSR0 = 0 to clear it
  explicitly. Error (0x10): SDK writes CSR0 = 0 and returns `-DEV_ERR_CONTROL`.
- **Timeouts / disconnect:** `get_jiffies()+500` (5 s; jiffies = 10 ms, `usb_mdelay`
  usb_phy.c:29) for each stage call (ctrl.c:92-98). Offline if DEVCTL bit2 = 0, or more than
  1000 consecutive SE0 samples (ctrl.c:99-108).
- **Toggle:** the SDK never touches EP0 toggles. INFERRED (MUSB): SetupPkt forces DATA0;
  data stage starts at DATA1; StatusPkt forces DATA1.
- **Status-OUT count:** for status OUT the SDK does not rewrite EP0_CNT (ctrl.c:76); it still
  holds 8 from the SETUP. INFERRED: write `EP_CNT(0) = 0` before `CSR0 = 0x42` to be safe. Open
  question 4.
- **SET_ADDRESS:** a normal no-data request with FADDR = 0 (setup `00 05 aa 00 00 00 00 00`,
  status IN). Only afterwards set FADDR = aa: the SDK stores devnum (usb_host.c:472) and the
  next `usb_ctlXfer` writes it. Wait 20 ms before the next request (usb_host.c:475).
- **CLEAR_FEATURE(ENDPOINT_HALT):** `usb_control_msg(.., USB_REQ_CLEAR_FEATURE(1),
  USB_RECIP_ENDPOINT(0x02), 0, ep_addr, NULL, 0)` (ctrl.c:259-262). No data, status IN.

---

## 6. (e) Bulk OUT / IN on one hardware endpoint

The SDK's MSC host uses host EP1 for both directions: `usb_get_ep_num(id, IN/OUT, BULK)`
(0x121e, bitmap bits 0x20000 / 0x2), called at `usb_storage.c:1991,1998`. EP1 TX and EP1 RX are
independent (separate CSRs, TADR/RADR, TYPE).

### 6.1 Endpoint config

`usb_h_ep_config(id, ep, BULK, ie=0, interval=0, ptr, dma_size)`, IR + usb.dis 0x12e4, called
at `usb_storage.c:1664-1670`:

```c
/* IN pipe (ep | 0x80) */
EP_RADR(1) = (u32)rxbuf;                    /* usb_set_dma_raddr */
sie_wr(INDEX,1); sie_wr(RXMAXP, 0xFF);
sie_wr(INDEX,1); sie_wr(RXCSR1, 0x90); sie_wr(RXCSR2, 0x00);   /* ClrDataTog | FlushFIFO */
/* bulk: usb_set_dma_dual_raddr(ptr) -> software ping-pong, see 6.3; no RXINTERVAL */
/* clear INTRRX1E bit1 */
/* OUT pipe */
EP_TADR(1) = (u32)txbuf;
sie_wr(INDEX,1); sie_wr(TXMAXP, 0xFF);
sie_wr(INDEX,1); sie_wr(TXCSR1, 0x48); sie_wr(TXCSR2, 0x00);   /* ClrDataTog | FlushFIFO */
/* no TXINTERVAL for bulk; clear INTRTX1E bit1 */
```

Buffers:

- TX: `txmaxp` (64).
- RX: `rxmaxp*2` (`usb_storage.c:1664`) + 8 for USB0 (`usb_epbuf_manager.c:46`), because of
  ping-pong and the 4-byte CRC tail per buffer.
- For a simple driver: one RX buffer of 64 + 4 bytes and one TX buffer of 64 bytes, both
  4-aligned in internal RAM. The SDK puts them in `.usb_h_dma` / `.usb_fifo` (`usb_data.ld`);
  the device driver uses plain aligned `.bss`.

### 6.2 Bulk OUT (`usb_h_ep_write`, usb.dis 0x1588-0x175a)

```c
sie_wr(FADDR, devaddr);                      /* already there from the last control transfer */
while (len) {                                /* SDK: do { } while (len); len==0 at entry sends one ZLP */
    n = min(len, 64);
    memcpy(txbuf, src, n); csync();          /* IR: memcpy + "csync" */
    sie_wr(INDEX,1); sie_wr(TXTYPE, 0x20 | target_ep);   /* usb_write_txtype (INDEX + 0x18) */
    EP_CNT(1) = n;  csync();                 /* usb_write_ep_cnt -> 0x1180C */
    sie_wr(INDEX,1); sie_wr(TXCSR1, 0x01); sie_wr(TXCSR2, 0x00);  /* usb_write_txcsr(..,1) */
    for (;;) {                               /* 0x1660-0x16c8 */
        /* SDK: timeout, offline (DEVCTL bit2, SE0 count) checks here */
        sie_wr(INDEX,1); c = sie_rd(TXCSR1) | sie_rd(TXCSR2) << 8;
        if (c & 0x80) { write_txcsr(c & ~0x80); goto resend; }       /* NAK timeout: resend same packet */
        if (c & 0x20) { write_txcsr(c & ~0x20); return STALL; }      /* SDK returns -16 */
        if (c & 0x04) { write_txcsr(c & ~0x04); goto resend; }       /* Error: resend */
        if (c & 0x01) continue;                                      /* still pending (NAKs retried by HW) */
        break;                                                       /* ACKed */
    }
    src += n; len -= n;
}
```

The async path that MSC actually uses (`usb_h_ep_write_async` 0x146c, kstart=1, USB0 branch
IR `do.body`) issues the same writes in another order: `EP_CNT(n)`, INDEX, TXTYPE, then only
TXCSR1 = 1.

### 6.3 Bulk IN (`usb0_h_ep_read`, usb.dis 0x1982-0x1bbe)

```c
sie_wr(INDEX,1); sie_wr(RXTYPE, 0x20 | target_ep); sie_wr(RXCSR1, 0x20);   /* ReqPkt (0x199c-0x19b0) */
for (;;) {
    /* SDK: timeout / offline checks */
    sie_wr(INDEX,1); c = sie_rd(RXCSR1);
    if (c & 0x01) {                                  /* RxPktRdy */
        n = sie_rd(RXCOUNT1) | sie_rd(RXCOUNT2) << 8;    /* 0x1a54-0x1a68; usb_read_rxcount does the same */
        ssync(); memcpy(dst, rxbuf, min(n, left));    /* IR: "ssync" then memcpy */
        sie_wr(RXCSR1, 0x10);                          /* release the buffer (FlushFIFO) */
        left -= min(n, left);
        if (left == 0 /* || n < 64: your short-packet rule, NOT in SDK */) break;
        sie_wr(RXTYPE, 0x20 | target_ep); sie_wr(RXCSR1, 0x20);   /* next packet (0x1b02-0x1b10) */
        continue;
    }
    if (c & 0x40) { sie_wr(RXCSR1, c & ~0x40 & 0xBF); /* SDK: then keeps polling, see notes */ }
    if (c & 0x08) { sie_wr(RXCSR1, 0x80); err = -11; } /* DataError / NAK timeout: SDK resets toggle */
    if (c & 0x04) { sie_wr(RXCSR1, 0x80); err = -6;  } /* Error */
}
```

- **Received length:** RXCOUNT1/2 (0x16/0x17, INDEX = ep), valid while RxPktRdy is set. EP0
  uses COUNT0 (0x16 with INDEX 0).
- **Releasing:** write RXCSR1 = 0x10. This clears RxPktRdy (written as 0) together with
  FlushFIFO, for both the sync and async paths (0x1850 with r5 = 16). The device driver does
  the same.
- **Ping-pong (SDK only).** `usb_set_dma_dual_raddr(ptr)` for bulk. `usb_get_dma_raddr`
  (0x52c) then alternates RADR between `ptr` and `ptr + rsize + 4`, before the release. So with
  `is_dual_buf` the SDK releases (0x10) before copying (IR `if.then94`). With one buffer: copy,
  then release.
- **Short packets:** the SDK does **not** end a read on a short packet. `usb0_h_ep_read`
  re-requests until `len` bytes arrived (IR `if.end102`: `if (sub92 != 0)` re-arm) and returns
  the requested `len`. The async ISR (`usb_bulk_rx_isr`, `usb_bulk_transfer.c:56-89`) only stops
  on a 0-length result. MSC code relies on exact lengths (CBW 31, CSW 13). Your driver should
  end on `n < 64`.
- **SDK STALL bug in sync read:** after RxStall the sync read clears the bit and goes back to
  polling without ReqPkt (IR `if.then115` → `while.cond.outer206`). It only leaves on the
  software timeout (-8). The async version returns -16 at once (`usb0_h_ep_read_async` IR
  `if.then38`). Treat RxStall as final.

### 6.4 STALL and the data toggle

- Detection: TXCSR1 bit5 (OUT) and RXCSR1 bit6 (IN). Clear by writing the CSR with the bit at 0.
- SDK recovery: `usb_bulk_only_receive` → on `-DEV_ERR_RXSTALL` (-16) send
  `usb_clear_feature(target_ep | 0x80)` (`usb_bulk_transfer.c:188-199`). `usb_bulk_only_send`
  does the same for -17 (`:296-308`).
  - The library returns -16 for a TX stall (`usb_h_ep_write` 0x173e; async 0x14dc). It returns
    -17 (`DEV_ERR_TXSTALL`, `device_drive.h:131-133`) for a TX *Error*. So the SDK clears the
    halt on a TX error, not on a TX stall.
  - **After CLEAR_FEATURE the SDK does not reset the host-side toggle.** Nothing in usb.c.o
    writes ClrDataTog except ep_config and the RX error path.
- What you need (INFERRED as correct per USB spec, using verified bits):

  ```c
  control: CLEAR_FEATURE(ENDPOINT_HALT) wIndex = 0x81 (IN) or 0x01/0x02 (OUT)   /* bmRequestType 0x02 */
  IN  pipe: sie_wr(INDEX,1); sie_wr(RXCSR1, 0x90);   /* ClrDataTog | FlushFIFO, as ep_config */
  OUT pipe: sie_wr(INDEX,1); sie_wr(TXCSR1, 0x48);   /* ClrDataTog | FlushFIFO */
  ```

  ClrDataTog puts the next packet at DATA0, which matches the device after CLEAR_FEATURE.
  For BOT reset recovery (class reset 0xFF, then CLEAR_FEATURE on both endpoints), do both.
- Setting DATA1 by software needs the unverified DataToggle/WrEnable bits (1.5). Not needed here.

### 6.5 NAK handling and timeouts

- **Hardware:** NAKed tokens are retried by the core. INFERRED: no NAK limit is programmed for
  bulk/EP0 (1.6), so the retries go on indefinitely. TXCSR1 bit7 / RXCSR1 bit3 (NAK timeout)
  then never fire. The SDK still handles them: TX resends the packet, RX writes 0x80.
- To get a hardware NAK timeout, write TXINTERVAL/RXINTERVAL (INDEX=ep, SIE 0x19 / 0x1B; for
  EP0 presumably 0x19 at INDEX 0). INFERRED (MUSB): value m = 2..16 gives 2^(m-1) frames;
  0/1 = off. Unverified on this core.
- **SDK software timeouts** (10 ms jiffies):
  - Sync bulk: deadline `jiffies + 200 + 2*(len/dma_size)` (≥ 2 s). On expiry it is re-armed
    up to 10 times, then returns `-DEV_ERR_WRITE_TIMEOUT` (-9) / `-DEV_ERR_READ_TIMEOUT` (-8);
    so about 22 s or more (`usb_h_ep_write` IR `if.then48`, `usb0_h_ep_read` IR `if.then29`).
    Each expiry also checks SE0 (>5 → -10 offline); reads log "DEV_ERR_READ_TIMEOUT reg = %x".
  - Async (used by MSC): `usb_sem_pend(host_dev, 300)` receive / `250` send →
    `-DEV_ERR_TIMEOUT` (`usb_bulk_transfer.c:120,254`).
  - Control: 5 s per stage (ctrl.c:92).
- **The SDK never cancels a pending transaction after a timeout.** There is no ReqPkt clear and
  no FlushFIFO; the next ep_config / sie_init cleans up. INFERRED abort for your driver:
  - RX: `RXCSR1 = 0x00` (drop ReqPkt), then `0x10`.
  - TX: `TXCSR1 = 0x08` (FlushFIFO).
  - EP0: `CSR0 = 0`; INFERRED: FlushFIFO lives in CSR0 high byte (SIE 0x12 at INDEX 0, MHDRC
    bit 8). Unverified.
- **usb_storage.c on a busy device:**
  - `usb_stor_init` retries INQUIRY / TEST UNIT READY / READ CAPACITY up to 5 times with
    `os_time_dly(30..50)` (300-500 ms) between tries, and REQUEST SENSE on a bad CSW
    (`usb_storage.c:1683-1748`).
  - Forced resets (`usb_stor_force_reset`) are commented out (`:1676, :1754`).
  - Resume path: GET_DESCRIPTOR; on failure `usb_host_force_reset` (`usb_io_reset`: pads to
    GPIO output-low with pulls off) + SET_ADDRESS (`:224-256`).

### 6.6 Interrupt registers when polling

INTRTX/INTRRX/INTRUSB keep latching with the enables at 0. INFERRED (MUSB): they clear on read.
You can ignore them, or read INTRUSB for Disconnect (bit5) / Babble (bit2). The SDK uses DEVCTL
bit2 and DP/DM SE0 as its offline test.

---

## 7. Timing summary (SDK values)

| What | Value | Source |
|---|---|---|
| after CON0/CON1 = 0 | ≥ 20 ms | fm1_usb.h:9 (device driver) |
| SIE reset pulse | `delay(10000)` | usb_h_sie_reset 0x20ca |
| pad / PHY settle | `delay(1000)` before and after `CON0 |= 0x25` | usb_h_sie_init |
| connect/speed poll | 10 ms steps, 500 ms timeout | chirp_and_reset, usb_common_def.h:85 |
| bus reset (SE0) | 40 ms (+10 ms per retry, 3 tries) | usb_common_def.h:83-84, usb_host.c:446 |
| after reset → first GET_DESCRIPTOR | 40 ms | usb_host.c:457 |
| before SET_ADDRESS | 20 ms | usb_host.c:468 |
| after SET_ADDRESS | 20 ms | usb_host.c:475 |
| control stage timeout | 5 s | usb_ctrl_transfer.c:92 |
| bulk timeouts | 2.5 s / 3 s async; ≥ 22 s sync | 6.5 |

`usb_mdelay(ms)` rounds to 10 ms OS ticks (`usb_phy.c:29`: `ms/10 + (ms%10 > 5)`, min 1).
So "1 ms" in the SDK is 10 ms.

---

## 8. Open questions (check on hardware)

1. **Data-toggle bits.** Do TXCSR2 bit0/bit1 and RXCSR2 bit1/bit2 (DataToggle / WrEnable, MHDRC
   layout) exist on USB0? Test: read RXCSR2 after successive bulk-IN packets and see bit1
   alternate. Not needed if ClrDataTog is enough.
2. **NAK limit.** What do TXINTERVAL/RXINTERVAL (and SIE 0x19 at INDEX 0 for EP0) do for bulk on
   this core: units and reset value? Do TXCSR1 bit7 / RXCSR1 bit3 / CSR0 bit7 ever fire?
3. **Cancelling a request.** Does writing RXCSR1 without ReqPkt cancel a pending IN token? Does
   TXCSR1 FlushFIFO cancel a pending OUT? Does the core clear ReqPkt on RxStall/Error?
4. **Status-OUT length.** The SDK leaves EP0_CNT at 8 for status-OUT. Does StatusPkt force a
   zero-length packet? (Safe choice: write EP0_CNT = 0.)
5. **Length of `delay(n)`.** It is a busy loop (`typedef.h:171`); the real settle times needed
   are unknown.
6. **POWER = 0x60 on USB0.** Bits 5 HSEnab / 6 SoftConn come from MUSB; is the write needed for
   a FS-only host?
7. **Unclear CON0 / IO bits.** CON0 bit3 TM1, bit5 VBUS and IO_CON0 bit12 SR: what are they, and
   are host-mode values (TM1 = 0, SR = 0) right for this board?
8. **Clock.** Is `CLK_CON1 &= ~3` enough after the mask ROM, or are CLK_CON1[11:10] = 01 /
   bit16 also needed (`clk_early_init`)?
9. **CRC tail on EP0 RX.** Does EP0 RX also write the 4-byte CRC tail? The SDK gives EP0 64 + 4.
10. **SOFs.** Do SOFs start automatically after the reset ends? Check CON0 bit13 / FRAME1-2.
11. **Toggle after STALL.** Does the core reset the toggle on RxStall? The SDK never re-clears it
    after CLEAR_FEATURE, which suggests either the hardware does it or the SDK is wrong.
12. **INTR registers.** Are INTRUSB/INTRTX/INTRRX clear-on-read here (MUSB convention)?
13. **No-cable SIE behaviour.** In device mode the SIE did not answer (CON1 bit15 never set)
    without a cable (`usb.c:1622`). Check that in host mode, with VBUS set by software, the SIE
    answers before a device is attached.
