
// bank-switching configuration
#define NES_MAPPER 4		// Mapper 4 (MMC3)
#define NES_PRG_BANKS 4		// # of 16KB PRG banks
#define NES_CHR_BANKS 1		// # of 8KB CHR banks

#include <peekpoke.h>
#include <string.h>
#include <nes.h>
#include "neslib.h"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// "strobe" means "write any value"
#define STROBE(addr) __asm__ ("sta %w", addr)

#define MMC3_IRQ_SET_VALUE(n) POKE(0xc000, (n));
#define MMC3_IRQ_RELOAD()     STROBE(0xc001)
#define MMC3_IRQ_DISABLE()    STROBE(0xe000)
#define MMC3_IRQ_ENABLE()     STROBE(0xe001)

void draw_text(word addr, const char* text) {
  vram_adr(addr);
  vram_write(text, strlen(text));
}

word counters[128];
byte irqcount = 0;

void __fastcall__ irq_nmi_callback(void) {
  // check high bit of A to see if this is an IRQ
  if (__A__ & 0x80) {
    // it's an IRQ from the MMC3 mapper
    // change PPU scroll registers
    PPU.scroll = counters[irqcount & 0x7f] >> 8;
    PPU.scroll = 0;
    // advance to next scroll value
    ++irqcount;
    // acknowledge interrupt
    MMC3_IRQ_DISABLE();
    MMC3_IRQ_ENABLE();
  } else {
    // this is a NMI
    // reload IRQ counter
    MMC3_IRQ_RELOAD();
    // reset scroll counter
    irqcount = 0;
  }
}

void main(void)
{
  // set up MMC3 IRQs every 8 scanlines
  MMC3_IRQ_SET_VALUE(7);
  MMC3_IRQ_RELOAD();
  MMC3_IRQ_ENABLE();
  // enable CPU IRQ
  __asm__ ("cli");
  // set IRQ callback
  nmi_set_callback(irq_nmi_callback);
  // set palette colors
  pal_col(1,0x04);
  pal_col(2,0x20);
  pal_col(3,0x30);
  // fill vram
  vram_adr(NTADR_A(0,0));
  vram_fill('A', 32*28);
  // turn on PPU/interrupts
  ppu_on_all();
  // loop forever, updating each counter at a different rate
  while(1) {
    byte i;
    for (i=0; i<128; i++) {
      counters[i] += i*16;
    }
    ppu_wait_frame();
  }
}
