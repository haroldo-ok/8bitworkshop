"use strict";

import { Platform, BaseZ80Platform  } from "../baseplatform";
import { PLATFORMS, RAM, newAddressDecoder, padBytes, noise, setKeyboardFromMap, AnimationTimer, RasterVideo, Keys, makeKeycodeMap } from "../emu";
import { hex, lzgmini, stringToByteArray, rgb2bgr, clamp } from "../util";
import { MasterAudio, AY38910_Audio } from "../audio";

// http://metopal.com/projects/ballybook/doku.php

const ASTROCADE_PRESETS = [
  {id:'01-helloworlds.asm', name:'Hello World'},
  {id:'02-telephone.asm', name:'Telephone'},
  {id:'03-horcbpal.asm', name:'Paddle Demo'},
  {id:'cosmic.c', name:'Cosmic Impalas Game'},
];

const ASTROCADE_BIOS_PRESETS = [
  {id:'bios.c', name:'BIOS'},
];

// TODO: fix keys, more controllers, vibrato/noise, border color, debug info, rotate
// http://atariage.com/forums/topic/251416-programming-the-bally-arcadeastrocade/

const ASTROCADE_KEYCODE_MAP = makeKeycodeMap([
  // player 1
  [Keys.VK_UP,    0x10, 0x1],
  [Keys.VK_DOWN,	0x10, 0x2],
  [Keys.VK_LEFT,  0x10, 0x4],
  [Keys.VK_RIGHT, 0x10, 0x8],
  [Keys.VK_SPACE, 0x10, 0x10],
  // keypad $14
  [Keys.VK_P,     0x14, 0x1],
  [Keys.VK_SLASH,	0x14, 0x2],
  [Keys.VK_X, 		0x14, 0x4],
  [Keys.VK_MINUS, 0x14, 0x8],
  [Keys.VK_COMMA, 0x14, 0x10],
  [Keys.VK_EQUALS,0x14, 0x20],
  // keypad $15
  [Keys.VK_Z,    	0x15, 0x1],
  [Keys.VK_H,			0x15, 0x2],
  [Keys.VK_9, 		0x15, 0x4],
  [Keys.VK_6,     0x15, 0x8],
  [Keys.VK_3,     0x15, 0x10],
  [Keys.VK_PERIOD,0x15, 0x20],
  // keypad $16
  [Keys.VK_A, 	  0x16, 0x1],
  [Keys.VK_S,			0x16, 0x2],
  [Keys.VK_8, 		0x16, 0x4],
  [Keys.VK_5,     0x16, 0x8],
  [Keys.VK_2,     0x16, 0x10],
  [Keys.VK_0,     0x16, 0x20],
  // keypad $17
  [Keys.VK_C,			0x17, 0x1],
  [Keys.VK_R,			0x17, 0x2],
  [Keys.VK_7, 		0x17, 0x4],
  [Keys.VK_4,     0x17, 0x8],
  [Keys.VK_1,     0x17, 0x10],
  [Keys.VK_E,			0x17, 0x20],
]);

const _BallyAstrocadePlatform = function(mainElement, arcade) {

  var cpu, ram, membus, iobus, rom, bios;
  var video, timer, pixels;
  var audio, psg;
  //var watchdog_counter;
  const swidth = arcade ? 320 : 160;
  const sheight = arcade ? 204 : 102;
  const swbytes = Math.floor(swidth / 4);
  const cpuFrequency = 1789000;
  const cpuCyclesPerLine = cpuFrequency/(60*sheight); // TODO: wait states?
  const INITIAL_WATCHDOG = 256;
  const PIXEL_ON = 0xffeeeeee;
  const PIXEL_OFF = 0xff000000;

  // state variables
  var inputs = new Uint8Array(0x20);
  var magicop = 0;
  var xpand = 0;
  var xplower = false;
  var shift2 = 0;
  var horcb = 0;
  var inmod = 0;
  var inlin = 0;
  var infbk = 0;
  var verbl = sheight;
  var palette = new Uint32Array(8);
  // default palette
  for (var i=0; i<8; i++)
    palette[i] = ASTROCADE_PALETTE[i];

  var refreshlines = 0;

  function ramwrite(a:number, v:number) {
    ram.mem[a] = v;
    ramupdate(a, v);
  }

  function ramupdate(a:number, v:number) {
    var ofs = a*4+3; // 4 pixels per byte
    for (var i=0; i<4; i++) {
      var lr = ((a % swbytes) >= (horcb & 0x3f)) ? 0 : 4;
      pixels[ofs--] = palette[lr + (v & 3)];
      v >>= 2;
    }
  }

  function refreshline(y:number) {
    var ofs = y*swidth/4;
    for (var i=0; i<swidth/4; i++)
      ramupdate(ofs+i, ram.mem[ofs+i]);
  }

  function magicwrite(a:number, v:number) {
    // expand
    if (magicop & 0x8) {
      var v2 = 0;
      if (!xplower)
        v >>= 4;
      for (var i=0; i<4; i++) {
        var pix = (v&1) ? ((xpand>>2)&3) : (xpand&3);
        v2 |= pix << (i*2);
        v >>= 1;
      }
      v = v2;
      xplower = !xplower;
    }
    // shift
    var sh = (magicop & 3) << 1;
    var v2 = (v >> sh) | shift2;
    shift2 = (v << (8-sh)) & 0xff;
    v = v2;
    // flop
    if (magicop & 0x40) {
      var v2 = 0;
      for (var i=0; i<4; i++) {
        v2 |= (v & 3) << (6-i*2);
        v >>= 2;
      }
      v = v2;
    }
    // or/xor
    if (magicop & 0x30) {
      var oldv = ram.mem[a];
      if (magicop & 0x10)
        v |= oldv;
      if (magicop & 0x20)
        v ^= oldv; // TODO: what if both?
      // collision detect
      var icpt = 0;
      for (var i=0; i<8; i+=2) {
        icpt <<= 1;
        // pixel changed from off to on?
        if ( !((oldv>>i)&3) && ((v>>i)&3) )
          icpt |= 1;
      }
      // upper 4 bits persist, lower are just since last write
      inputs[8] = (inputs[8] & 0xf0) | icpt | (icpt<<4);
    }
    // commit write to ram/screen
    ramwrite(a, v);
  }

  function setpalette(a:number, v:number) {
    palette[a&7] = ASTROCADE_PALETTE[v&0xff];
    refreshall();
  }

  function setbordercolor() {
    var col = horcb >> 6;
    // TODO
  }

  function refreshall() {
    refreshlines = sheight;
  }

 class BallyAstrocadePlatform extends BaseZ80Platform implements Platform {

  getPresets() {
    return ASTROCADE_PRESETS;
  }

  start = function() {
    ram = new RAM(arcade ? 0x5000 : 0x1000);
    bios = padBytes(ASTROCADE_MINIMAL_BIOS, 0x2000);
    if (!arcade) {
      // game console
      membus = {
        read: newAddressDecoder([
          [0x0000, 0x1fff, 0x1fff, function(a) { return bios[a]; }],
          [0x2000, 0x3fff, 0x1fff, function(a) { return rom ? rom[a] : 0; }],
          [0x4000, 0x4fff,  0xfff, function(a) { return ram.mem[a]; }],
        ]),
        write: newAddressDecoder([
          [0x4000, 0x4fff,  0xfff, ramwrite],
          [0x0000, 0x3fff, 0x3fff, magicwrite],
        ]),
        isContended: function() { return false; },
      };
    } else {
      // arcade game
      membus = {
        read: newAddressDecoder([
          [0x4000, 0x7fff, 0x3fff, function(a) { return ram.mem[a]; }],	// screen RAM
          [0xd000, 0xdfff,  0xfff, function(a) { return ram.mem[a+0x4000]; }], // static RAM
          [0x0000, 0xafff, 0xffff, function(a) { return rom ? rom[a] : 0; }], // ROM (0-3fff,8000-afff)
        ]),
        write: newAddressDecoder([
          [0x4000, 0x7fff, 0x3fff, ramwrite],
          [0xd000, 0xdfff,  0xfff, function(a,v) { ramwrite(a+0x4000, v); } ], // static RAM
          [0x0000, 0x3fff, 0x3fff, magicwrite],
        ]),
        isContended: function() { return false; },
      };
    }
    iobus = {
      read: function(addr) {
    	  addr &= 0x1f;
    	  var rtn = inputs[addr];
    	  if (addr == 8)
    	    inputs[addr] = 0;
        // $10 = watchdog
    	  return rtn;
    	},
    	write: function(addr, val) {
    	  addr &= 0x1f;
				val &= 0xff;
        switch (addr) {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
            setpalette(addr,val);
            break;
          case 9:   // HORCB (horizontal boundary byte)
            horcb = val;
            setbordercolor();
            refreshall();
            break;
          case 0xa: // VERBL (vertical blank)
            verbl = arcade ? val : val >> 1;
            refreshall();
            break;
          case 0xb: // OTIR (set palette)
             setpalette(cpu.getBC()>>8, membus.read(cpu.getHL()));
             break;
          case 0xc: // magic register
            magicop = val;
            shift2 = 0;
            break;
          case 0xd: // INFBK (interrupt feedback)
            infbk = val;
            break;
          case 0xe: // INMOD (interrupt enable)
            inmod = val;
            break;
          case 0xf: // INLIN (interrupt line)
            inlin = arcade ? val : val >> 1;
            break;
          case 0x10:
          case 0x11:
          case 0x12:
          case 0x13:
          case 0x14:
          case 0x15:
          case 0x16:
          case 0x17:
            psg.setACRegister(addr, val);
            break;
          case 0x18:
            psg.setACRegister(cpu.getBC()>>8, membus.read(cpu.getHL()));
            break;
          case 0x19: // XPAND
            xpand = val;
            xplower = false;
            break;
          default:
            console.log('IO write', hex(addr,4), hex(val,2));
            break;
        }
    	}
    };
    cpu = this.newCPU(membus, iobus);
    audio = new MasterAudio();
    psg = new AstrocadeAudio(audio);
    video = new RasterVideo(mainElement,swidth,sheight,{});
    video.create();
    video.setupMouseEvents();
    var idata = video.getFrameData();
		setKeyboardFromMap(video, inputs, ASTROCADE_KEYCODE_MAP);
    pixels = video.getFrameData();
    timer = new AnimationTimer(60, this.nextFrame.bind(this));
  }

  readAddress(addr) {
    return membus.read(addr);
  }

  loadControls() {
    inputs[0x1c] = video.paddle_x & 0xff;
    inputs[0x1d] = video.paddle_y & 0xff;
  }

  advance(novideo : boolean) {
    this.loadControls();
    for (var sl=0; sl<sheight; sl++) {
      //console.log(sl, hex(cpu.getPC(),4), cpu.saveState());
      this.runCPU(cpu, cpuCyclesPerLine);
      if (sl == inlin && (inmod & 0x8)) {
        this.requestInterrupt(cpu, infbk);
      }
      if (refreshlines>0) {
        refreshline(sl);
        refreshlines--;
      }
    }
    if (!novideo) {
      video.updateFrame(0, 0, 0, 0, swidth, verbl);
    }
    /*
    if (watchdog_counter-- <= 0) {
      console.log("WATCHDOG FIRED"); // TODO: alert on video
      this.reset();
    }
    */
  }

  loadROM(title, data) {
    rom = padBytes(data, arcade ? 0xb000 : 0x2000);
    this.reset();
  }

  loadBIOS(title, data) {
    bios = padBytes(data, 0x2000);
    this.reset();
  }

  loadState(state) {
    cpu.loadState(state.c); // TODO: this causes problems on reset+debug
    ram.mem.set(state.b);
    palette.set(state.palette);
    magicop = state.magicop;
    xpand = state.xpand;
    xplower = state.xplower;
    shift2 = state.shift2;
    horcb = state.horcb;
    inmod = state.inmod;
    inlin = state.inlin;
    infbk = state.infbk;
    verbl = state.verbl;
    this.loadControlsState(state);
    refreshall();
  }
  saveState() {
    return {
      c: this.getCPUState(),
      b: ram.mem.slice(0),
      in: inputs.slice(0),
      palette: palette.slice(0),
      magicop: magicop,
      xpand: xpand,
      xplower: xplower,
      shift2: shift2,
      horcb: horcb,
      inmod: inmod,
      inlin: inlin,
      infbk: infbk,
      verbl: verbl,
    };
  }
  loadControlsState(state) {
    inputs.set(state.in);
  }
  saveControlsState() {
    return {
      in:inputs.slice(0)
    };
  }
  getCPUState() {
    return cpu.saveState();
  }

  isRunning() {
    return timer && timer.isRunning();
  }
  pause() {
    timer.stop();
    audio.stop();
  }
  resume() {
    timer.start();
    audio.start();
  }
  reset() {
    cpu.reset();
    cpu.setTstates(0);
    //watchdog_counter = INITIAL_WATCHDOG;
  }
 }
  return new BallyAstrocadePlatform();
}

/////

class AstrocadeAudio extends AY38910_Audio {
  setACRegister(addr : number, val : number) {
    addr &= 0x7;
    val &= 0xff;
    //console.log(addr,val);
    switch (addr) {
      case 0:
        this.psg.setClock(1789000 * 16 / (val + 1));
        this.psg.writeRegisterAY(7, 0x7 ^ 0xff); // disable noise
        break;
      case 1:
      case 2:
      case 3:
        var i = (addr-1)*2;
        var j = val*2+1;
        this.psg.writeRegisterAY(i, j&0xff); // freq lo
        this.psg.writeRegisterAY(i+1, (j>>8)&0xff); // freq hi
        break;
      case 5:
        this.psg.writeRegisterAY(10, val & 0xf); // tone c vol
        break;
      case 6:
        this.psg.writeRegisterAY(8, val & 0xf);	// tone a vol
        this.psg.writeRegisterAY(9, (val>>4) & 0xf); // tone b vol
        break;
    }
  }
}

const _BallyArcadePlatform = function(mainElement) {
  this.__proto__ = new (_BallyAstrocadePlatform as any)(mainElement, true);
  // TODO: inputs[0x13] = 0xfe; // dip switch on arcade
  // TODO: arcade controls, bit blit
  var _in = this.saveControlsState();
  _in.in[0x10] = 0xff; // switches
  _in.in[0x13] = 0xfe; // dip switches
  this.loadControlsState(_in);
}

const _BallyAstrocadeBIOSPlatform = function(mainElement) {
  this.__proto__ = new (_BallyAstrocadePlatform as any)(mainElement);

  this.getPresets = function() {
    return ASTROCADE_BIOS_PRESETS;
  }

  this.loadROM = this.loadBIOS;
}

/////

PLATFORMS['astrocade'] = _BallyAstrocadePlatform;
PLATFORMS['astrocade-bios'] = _BallyAstrocadeBIOSPlatform;
PLATFORMS['astrocade-arcade'] = _BallyArcadePlatform;

//http://glankonian.com/~lance/astrocade_palette.html
var ASTROCADE_PALETTE = [0x000000,0x242424,0x484848,0x6D6D6D,0x919191,0xB6B6B6,0xDADADA,0xFFFFFF,0x2500BB,0x4900E0,0x6E11FF,0x9235FF,0xB75AFF,0xDB7EFF,0xFFA3FF,0xFFC7FF,0x4900B0,0x6D00D5,0x9201F9,0xB625FF,0xDA4AFF,0xFF6EFF,0xFF92FF,0xFFB7FF,0x6A009F,0x8E00C3,0xB300E7,0xD718FF,0xFB3CFF,0xFF61FF,0xFF85FF,0xFFA9FF,0x870087,0xAB00AB,0xD000D0,0xF40EF4,0xFF32FF,0xFF56FF,0xFF7BFF,0xFF9FFF,0x9F006A,0xC3008E,0xE700B3,0xFF07D7,0xFF2CFB,0xFF50FF,0xFF74FF,0xFF99FF,0xB00049,0xD5006D,0xF90092,0xFF05B6,0xFF29DA,0xFF4DFF,0xFF72FF,0xFF96FF,0xBB0025,0xE00049,0xFF006E,0xFF0692,0xFF2AB7,0xFF4FDB,0xFF73FF,0xFF98FF,0xBF0000,0xE30024,0xFF0048,0xFF0B6D,0xFF3091,0xFF54B6,0xFF79DA,0xFF9DFF,0xBB0000,0xE00000,0xFF0023,0xFF1447,0xFF396C,0xFF5D90,0xFF82B5,0xFFA6D9,0xB00000,0xD50000,0xF90000,0xFF2124,0xFF4548,0xFF6A6C,0xFF8E91,0xFFB3B5,0x9F0000,0xC30000,0xE70C00,0xFF3003,0xFF5527,0xFF794B,0xFF9E70,0xFFC294,0x870000,0xAB0000,0xD01E00,0xF44200,0xFF670A,0xFF8B2E,0xFFAF53,0xFFD477,0x6A0000,0x8E0D00,0xB33100,0xD75600,0xFB7A00,0xFF9E17,0xFFC33B,0xFFE75F,0x490000,0x6D2100,0x924500,0xB66A00,0xDA8E00,0xFFB305,0xFFD729,0xFFFC4E,0x251100,0x493500,0x6E5A00,0x927E00,0xB7A300,0xDBC700,0xFFEB1E,0xFFFF43,0x002500,0x244900,0x486D00,0x6D9200,0x91B600,0xB6DB00,0xDAFF1B,0xFFFF3F,0x003700,0x005B00,0x238000,0x47A400,0x6CC900,0x90ED00,0xB5FF1E,0xD9FF43,0x004700,0x006C00,0x009000,0x24B400,0x48D900,0x6CFD05,0x91FF29,0xB5FF4E,0x005500,0x007900,0x009D00,0x03C200,0x27E600,0x4BFF17,0x70FF3B,0x94FF5F,0x005F00,0x008300,0x00A800,0x00CC00,0x0AF00A,0x2EFF2E,0x53FF53,0x77FF77,0x006500,0x008A00,0x00AE00,0x00D203,0x00F727,0x17FF4B,0x3BFF70,0x5FFF94,0x006800,0x008C00,0x00B100,0x00D524,0x00F948,0x05FF6C,0x29FF91,0x4EFFB5,0x006600,0x008B00,0x00AF23,0x00D447,0x00F86C,0x00FF90,0x1EFFB5,0x43FFD9,0x006100,0x008524,0x00AA48,0x00CE6D,0x00F391,0x00FFB6,0x1BFFDA,0x3FFFFE,0x005825,0x007C49,0x00A16E,0x00C592,0x00EAB7,0x00FFDB,0x1EFFFF,0x43FFFF,0x004B49,0x00706D,0x009492,0x00B9B6,0x00DDDA,0x05FFFF,0x29FFFF,0x4EFFFF,0x003C6A,0x00608E,0x0085B3,0x00A9D7,0x00CEFB,0x17F2FF,0x3BFFFF,0x5FFFFF,0x002A87,0x004FAB,0x0073D0,0x0097F4,0x0ABCFF,0x2EE0FF,0x53FFFF,0x77FFFF,0x00179F,0x003BC3,0x0060E7,0x0384FF,0x27A8FF,0x4BCDFF,0x70F1FF,0x94FFFF,0x0002B0,0x0027D5,0x004BF9,0x2470FF,0x4894FF,0x6CB9FF,0x91DDFF,0xB5FFFF,0x0000BB,0x0013E0,0x2337FF,0x475BFF,0x6C80FF,0x90A4FF,0xB5C9FF,0xD9EDFF];
// swap palette RGB to BGR
for (var i=0; i<256; i++) {
  var x = ASTROCADE_PALETTE[i];
  ASTROCADE_PALETTE[i] = rgb2bgr(x) | 0xff000000;
}

/*
0000 F3            [ 4]  196 	DI	; disable interrupts
0001 21 00 20      [10]  197 	LD	HL,#0x2000
0004 7E            [ 7]  198 	LD	A,(HL) ; A <- mem[0x2000]
0005 FE 55         [ 7]  199 	CP	#0x55 ; found sentinel byte? ($55)
0007 CA 0D 00      [10]  200 	JP	Z,FoundSentinel ; yes, load program
000A C3 AB 0E      [10]  201 	JP	_main ; jump to test program
000D                     202 	FoundSentinel:
000D 31 CE 4F      [10]  203 	LD	SP,#0x4fce ; position stack below BIOS vars
0010 CD 84 02      [17]  204 	CALL	_bios_init ; misc. bios init routines
0013 21 05 20      [10]  205 	LD	HL,#0x2005 ; cartridge start vector
0016 7E            [ 7]  206 	LD	A,(HL)
0017 23            [ 6]  207 	INC	HL
0018 66            [ 7]  208 	LD	H,(HL)
0019 6F            [ 4]  209 	LD	L,A
001A E9            [ 4]  210 	JP	(HL) ; jump to cart start vector
*/
var ASTROCADE_MINIMAL_BIOS = [
  0xf3, 0x21, 0x00, 0x20, 0x7e, 0xfe, 0x55, 0xca, 0x0d, 0x00, 0xc3, 0xab, 0x0e,
  0x31, 0xce, 0x4f, 0xcd, 0x84, 0x02, 0x21, 0x05, 0x20, 0x7e, 0x23, 0x66, 0x6f,
  0xe9,
];

