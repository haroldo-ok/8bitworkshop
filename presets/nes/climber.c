
#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// famitone2 library
//#link "famitone2.s"

// music and sfx
//#link "music_dangerstreets.s"
extern char danger_streets_music_data[];
//#link "demosounds.s"
extern char demo_sounds[];

// indices of sound effects (0..3)
typedef enum { SND_START, SND_HIT, SND_COIN, SND_JUMP } SFXIndex;

///// DEFINES

#define COLS 30		// floor width in tiles
#define ROWS 60		// total nametable height in tiles

#define MAX_FLOORS 4	// total # of floors in a stage
#define GAPSIZE 4	// gap size in tiles
#define BOTTOM_FLOOR_Y 2	// offset for bottommost floor

#define MAX_ACTORS 8		// max # of moving actors
#define SCREEN_Y_BOTTOM 208	// bottom of screen in pixels
#define ACTOR_MIN_X 16		// leftmost position of actor
#define ACTOR_MAX_X 228		// rightmost position of actor
#define ACTOR_SCROLL_UP_Y 110	// min Y position to scroll up
#define ACTOR_SCROLL_DOWN_Y 140	// max Y position to scroll down
#define JUMP_VELOCITY 18	// Y velocity when jumping

// constants for various tiles
#define CH_BORDER 0x40
#define CH_FLOOR 0xf4
#define CH_LADDER 0xd4
#define CH_ITEM 0xc4
#define CH_BLANK 0x20

///// GLOBALS

// vertical scroll amount in pixels
static int scroll_pixel_yy = 0;

// vertical scroll amount in tiles
static byte scroll_tile_y = 0;

// last screen Y position of player sprite
static byte player_screen_y = 0;

// score (BCD)
static byte score = 0;

// screen flash animation (virtual bright)
static byte vbright = 4;

// random byte between (a ... b-1)
// use rand() because rand8() has a cycle of 255
byte rndint(byte a, byte b) {
  return (rand() % (b-a)) + a;
}

// return nametable address for tile (x,y)
// assuming vertical scrolling (horiz. mirroring)
word getntaddr(byte x, byte y) {
  word addr;
  if (y < 30) {
    addr = NTADR_A(x,y);
  } else {
    addr = NTADR_C(x,y-30);
  }
  return addr;
}

// convert nametable address to attribute address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

/// METASPRITES

// define a 2x2 metasprite
#define DEF_METASPRITE_2x2(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code)+0,   pal, \
        0,      8,      (code)+1,   pal, \
        8,      0,      (code)+2,   pal, \
        8,      8,      (code)+3,   pal, \
        128};

// define a 2x2 metasprite, flipped horizontally
#define DEF_METASPRITE_2x2_FLIP(name,code,pal)\
const unsigned char name[]={\
        8,      0,      (code)+0,   (pal)|OAM_FLIP_H, \
        8,      8,      (code)+1,   (pal)|OAM_FLIP_H, \
        0,      0,      (code)+2,   (pal)|OAM_FLIP_H, \
        0,      8,      (code)+3,   (pal)|OAM_FLIP_H, \
        128};

DEF_METASPRITE_2x2(playerRStand, 0xd8, 0);
DEF_METASPRITE_2x2(playerRRun1, 0xdc, 0);
DEF_METASPRITE_2x2(playerRRun2, 0xe0, 0);
DEF_METASPRITE_2x2(playerRRun3, 0xe4, 0);
DEF_METASPRITE_2x2(playerRJump, 0xe8, 0);
DEF_METASPRITE_2x2(playerRClimb, 0xec, 0);
DEF_METASPRITE_2x2(playerRSad, 0xf0, 0);

DEF_METASPRITE_2x2_FLIP(playerLStand, 0xd8, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun1, 0xdc, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun2, 0xe0, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun3, 0xe4, 0);
DEF_METASPRITE_2x2_FLIP(playerLJump, 0xe8, 0);
DEF_METASPRITE_2x2_FLIP(playerLClimb, 0xec, 0);
DEF_METASPRITE_2x2_FLIP(playerLSad, 0xf0, 0);

//DEF_METASPRITE_2x2(personToSave, 0xba, 1);
const unsigned char personToSave[]={
        0,      0,      (0xba)+0,   3, 
        0,      8,      (0xba)+2,   0, 
        8,      0,      (0xba)+1,   3, 
        8,      8,      (0xba)+3,   0, 
        128};

const unsigned char* const playerRunSeq[16] = {
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2,
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2,
};

///// GAME LOGIC

// struct definition for a single floor
typedef struct Floor {
  byte ypos;		// # of tiles from ground
  int height:4;		// # of tiles to next floor
  int gap:4;		// X position of gap
  int ladder1:4;	// X position of first ladder
  int ladder2:4;	// X position of second ladder
  int objtype:4;	// item type (FloorItem)
  int objpos:4;		// X position of object
} Floor;

// various items the player can pick up
typedef enum FloorItem { ITEM_NONE, ITEM_MINE, ITEM_HEART, ITEM_POWER };

// array of floors
Floor floors[MAX_FLOORS];

// is this x (pixel) position within the gap <gap>?
bool is_in_gap(byte x, byte gap) {
  if (gap) {
    byte x1 = gap*16 + 4;
    return (x > x1 && x < x1+GAPSIZE*8-4);
  } else {
    return false;
  }
}

// is this ladder at (tile) position x within the gap?
bool ladder_in_gap(byte x, byte gap) {
  return gap && x >= gap && x < gap+GAPSIZE*2;
}

// create floors at start of game
void make_floors() {
  byte i;
  byte y = BOTTOM_FLOOR_Y;
  Floor* prevlev = &floors[0];
  for (i=0; i<MAX_FLOORS; i++) {
    Floor* lev = &floors[i];
    lev->height = rndint(2,5)*2;
    do {
      // only have gaps in higher floors
      lev->gap = i>=5 ? rndint(0,13) : 0;
    } while (ladder_in_gap(prevlev->ladder1, lev->gap) || 
             ladder_in_gap(prevlev->ladder2, lev->gap));
    do {
      lev->ladder1 = rndint(1,14);
      lev->ladder2 = rndint(1,14);
    } while (ladder_in_gap(lev->ladder1, lev->gap) || 
             ladder_in_gap(lev->ladder2, lev->gap));
    if (i > 0) {
      lev->objtype = rndint(1,4);
      do {
        lev->objpos = rndint(1,14);
      } while (ladder_in_gap(lev->objpos, lev->gap));
    }
    lev->ypos = y;
    y += lev->height;
    prevlev = lev;
  }
  // top floor is special
  floors[MAX_FLOORS-1].height = 15;
  floors[MAX_FLOORS-1].gap = 0;
  floors[MAX_FLOORS-1].ladder1 = 0;
  floors[MAX_FLOORS-1].ladder2 = 0;
  floors[MAX_FLOORS-1].objtype = 0;
}

void create_actors_on_floor(byte floor_index);

// draw a nametable line into the frame buffer at <screen_y>
// 0 == bottom of stage
void draw_floor_line(byte screen_y) {
  char buf[COLS];
  char attrs[8];
  byte floor, i;
  byte y = screen_y;
  byte rowy;
  word addr;
  for (floor=0; floor<MAX_FLOORS; floor++) {
    Floor* lev = &floors[floor];
    byte dy = y - lev->ypos;
    if (dy >= 254) dy = 0; // if below BOTTOM_Y_FLOOR
    // is this floor visible on-screen?
    if (dy < lev->height) {
      if (dy <= 1) {
        // draw floor
        for (i=0; i<COLS; i+=2) {
          if (dy) {
            buf[i] = CH_FLOOR;
            buf[i+1] = CH_FLOOR+2;
          } else {
            buf[i] = CH_FLOOR+1;
            buf[i+1] = CH_FLOOR+3;
          }
        }
        // draw the gap
	if (lev->gap)
          memset(buf+lev->gap*2, 0, GAPSIZE);
      } else {
        // draw empty space
        memset(buf, 0, sizeof(buf));
        // draw walls
        if (floor < MAX_FLOORS-1) {
          buf[0] = CH_FLOOR+1;
          buf[COLS-1] = CH_FLOOR;
        }
        // draw ladders
        if (lev->ladder1) {
          buf[lev->ladder1*2] = CH_LADDER;
          buf[lev->ladder1*2+1] = CH_LADDER+1;
        }
        if (lev->ladder2) {
          buf[lev->ladder2*2] = CH_LADDER;
          buf[lev->ladder2*2+1] = CH_LADDER+1;
        }
      }
      // draw object, if it exists
      if (lev->objtype) {
        byte ch = lev->objtype*4 + CH_ITEM;
        if (dy == 2) {
          buf[lev->objpos*2] = ch+1;
          buf[lev->objpos*2+1] = ch+3;
        }
        else if (dy == 3) {
          buf[lev->objpos*2] = ch+0;
          buf[lev->objpos*2+1] = ch+2;
        }
      }
      // compute row in name buffer and address
      rowy = (ROWS-1) - (screen_y%60);
      addr = getntaddr(1, rowy);
      // copy attribute table (every 4th row)
      if ((addr & 0x60) == 0) {
        byte a;
        if (dy==1)
          a = 0x05;
        else if (dy==3)
          a = 0x50;
        else
          a = 0x00;
        memset(attrs, a, 8);
        vrambuf_put(nt2attraddr(addr), attrs, 8);
      }
      // copy line to screen buffer
      vrambuf_put(addr, buf, COLS);
      // create actors on this floor, if needed
      // TODO: maybe this happens too early?
      if (dy == 0 && (floor >= 2)) {
        create_actors_on_floor(floor);
      }
      break;
    }
  }
}

// draw entire stage at current scroll position
// filling up entire name table
void draw_entire_stage() {
  byte y;
  for (y=0; y<ROWS; y++) {
    draw_floor_line(y);
    // allow buffer to flush, delaying a frame
    vrambuf_flush();
  }
}

// get Y pixel position for a given floor
word get_floor_yy(byte floor) {
  return floors[floor].ypos * 8 + 16;
}

// get Y ceiling position for a given floor
word get_ceiling_yy(byte floor) {
  return (floors[floor].ypos + floors[floor].height) * 8 + 16;
}

// set scrolling position
void set_scroll_pixel_yy(int yy) {
  // draw an offscreen line, every 8 pixels
  if ((yy & 7) == 0) {
    // scrolling upward or downward?
    if (yy > scroll_pixel_yy)
      draw_floor_line(scroll_tile_y+30);
    else
      draw_floor_line(scroll_tile_y-30);
  }
  scroll_pixel_yy = yy;
  scroll_tile_y = yy >> 3; // divide by 8
  scroll(0, 479 - ((yy + 224) % 480));
}

// redraw a floor when object picked up
void refresh_floor(byte floor) {
  byte y = floors[floor].ypos;
  draw_floor_line(y+2);
  draw_floor_line(y+3);
}

///// ACTORS

typedef enum ActorState {
  INACTIVE, STANDING, WALKING, CLIMBING, JUMPING, FALLING, PACING
};

typedef enum ActorType {
  ACTOR_PLAYER, ACTOR_ENEMY, ACTOR_RESCUE
};

typedef struct Actor {
  word yy;		// Y position in pixels (16 bit)
  byte x;		// X position in pixels (8 bit)
  byte floor;		// floor index
  byte state;		// ActorState
  int name:2;		// ActorType (2 bits)
  int pal:2;		// palette color (2 bits)
  int dir:1;		// direction (0=right, 1=left)
  int onscreen:1;	// is actor onscreen?
  union {
    struct {		// when jumping...
      sbyte yvel;	// Y velocity
      sbyte xvel;	// X velocity
    } jumping;
  } u;
} Actor;

Actor actors[MAX_ACTORS];

void create_actors_on_floor(byte floor_index) {
  byte actor_index = (floor_index % (MAX_ACTORS-1)) + 1;
  struct Actor* a = &actors[actor_index];
  if (!a->onscreen) {
    Floor *floor = &floors[floor_index];
    a->state = STANDING;
    a->name = ACTOR_ENEMY;
    a->x = rand8();
    a->yy = get_floor_yy(floor_index);
    a->floor = floor_index;
    a->onscreen = 1;
    // rescue person on top of the building
    if (floor_index == MAX_FLOORS-1) {
      a->name = ACTOR_RESCUE;
      a->state = PACING;
      a->x = 0;
      a->pal = 1;
    }
  }
}

void draw_actor(byte i) {
  struct Actor* a = &actors[i];
  bool dir;
  const unsigned char* meta;
  byte x,y; // sprite variables
  int screen_y = SCREEN_Y_BOTTOM - a->yy + scroll_pixel_yy;
  if (screen_y > 192+8 || screen_y < -18) {
    a->onscreen = 0;
    return; // offscreen vertically
  }
  dir = a->dir;
  switch (a->state) {
    case INACTIVE:
      a->onscreen = 0;
      return; // inactive, offscreen
    case STANDING:
      meta = dir ? playerLStand : playerRStand;
      break;
    case WALKING:
      meta = playerRunSeq[((a->x >> 1) & 7) + (dir?0:8)];
      break;
    case JUMPING:
      meta = dir ? playerLJump : playerRJump;
      break;
    case FALLING:
      meta = dir ? playerLSad : playerRSad;
      break;
    case CLIMBING:
      meta = (a->yy & 4) ? playerLClimb : playerRClimb;
      break;
    case PACING:
      meta = personToSave;
      break;
  }
  // set sprite values
  x = a->x;
  y = screen_y;
  oam_meta_spr_pal(x, y, a->pal, meta);
  // is this actor 0? (player sprite)
  if (i == 0) {
    player_screen_y = y; // last screen Y position
  }
  a->onscreen = 1;
  return;
}

void draw_scoreboard() {
  oam_off = oam_spr(24+0, 24, '0'+(score >> 4), 2, oam_off);
  oam_off = oam_spr(24+8, 24, '0'+(score & 0xf), 2, oam_off);
}

void refresh_sprites() {
  byte i;
  // reset sprite index to 0
  oam_off = 0;
  // draw all actors
  for (i=0; i<MAX_ACTORS; i++)
    draw_actor(i);
  // draw scoreboard
  draw_scoreboard();
  // hide rest of actors
  oam_hide_rest(oam_off);
}

byte is_ladder_close(byte actor_x, byte ladder_pos) {
  byte ladder_x;
  if (ladder_pos == 0)
    return 0;
  ladder_x = ladder_pos * 16;
  return ((byte)(actor_x - ladder_x) < 16) ? ladder_x : 0;
}

byte get_closest_ladder(byte player_x, byte floor_index) {
  Floor* floor = &floors[floor_index];
  byte x;
  if (floor_index >= MAX_FLOORS) return 0;
  x = is_ladder_close(player_x, floor->ladder1);
  if (x) return x;
  x = is_ladder_close(player_x, floor->ladder2);
  if (x) return x;
  return 0;
}

byte mount_ladder(Actor* player, signed char floor_adjust) {
  byte x = get_closest_ladder(player->x, player->floor + floor_adjust);
  if (x) {
    player->x = x + 8;
    player->state = CLIMBING;
    player->floor += floor_adjust;
    return 1;
  } else
    return 0;
}

void check_scroll_up() {
  if (player_screen_y < ACTOR_SCROLL_UP_Y) {
    set_scroll_pixel_yy(scroll_pixel_yy + 1);
  }
}

void check_scroll_down() {
  if (player_screen_y > ACTOR_SCROLL_DOWN_Y && scroll_pixel_yy > 0) {
    set_scroll_pixel_yy(scroll_pixel_yy - 1);
  }
}

void fall_down(struct Actor* actor) {
  actor->floor--;
  actor->state = FALLING;
  actor->u.jumping.xvel = 0;
  actor->u.jumping.yvel = 0;
}

void move_actor(struct Actor* actor, byte joystick, bool scroll) {
  switch (actor->state) {
      
    case STANDING:
    case WALKING:
      // left/right has priority over climbing
      if (joystick & PAD_A) {
        actor->state = JUMPING;
        actor->u.jumping.xvel = 0;
        actor->u.jumping.yvel = JUMP_VELOCITY;
        if (joystick & PAD_LEFT) actor->u.jumping.xvel = -1;
        if (joystick & PAD_RIGHT) actor->u.jumping.xvel = 1;
        // play sound for player
        if (scroll) sfx_play(SND_JUMP,0);
      } else if (joystick & PAD_LEFT) {
        actor->x--;
        actor->dir = 1;
        actor->state = WALKING;
      } else if (joystick & PAD_RIGHT) {
        actor->x++;
        actor->dir = 0;
        actor->state = WALKING;
      } else if (joystick & PAD_UP) {
        mount_ladder(actor, 0); // state -> CLIMBING
      } else if (joystick & PAD_DOWN) {
        mount_ladder(actor, -1); // state -> CLIMBING, floor -= 1
      } else {
        actor->state = STANDING;
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
      break;
      
    case CLIMBING:
      if (joystick & PAD_UP) {
      	if (actor->yy >= get_ceiling_yy(actor->floor)) {
          actor->floor++;
          actor->state = STANDING;
        } else {
          actor->yy++;
        }
      } else if (joystick & PAD_DOWN) {
        if (actor->yy <= get_floor_yy(actor->floor)) {
          actor->state = STANDING;
        } else {
          actor->yy--;
        }
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
      break;
      
    case FALLING:
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
    case JUMPING:
      actor->x += actor->u.jumping.xvel;
      actor->yy += actor->u.jumping.yvel/4;
      actor->u.jumping.yvel -= 1;
      if (actor->yy <= get_floor_yy(actor->floor)) {
	actor->yy = get_floor_yy(actor->floor);
        actor->state = STANDING;
      }
      break;
  }
  // don't allow player to travel past left/right edges of screen
  if (actor->x > ACTOR_MAX_X) actor->x = ACTOR_MAX_X; // we wrapped around right edge
  if (actor->x < ACTOR_MIN_X) actor->x = ACTOR_MIN_X;
  // if player lands in a gap, they fall (switch to JUMPING state)
  if (actor->state <= WALKING && 
      is_in_gap(actor->x, floors[actor->floor].gap)) {
    fall_down(actor);
  }
}

void pickup_object(Actor* actor) {
  Floor* floor = &floors[actor->floor];
  byte objtype = floor->objtype;
  if (objtype && actor->state <= WALKING) {
    byte objx = floor->objpos * 16;
    if (actor->x >= objx && actor->x < objx+16) {
      // clear the item from the floor and redraw
      floor->objtype = 0;
      refresh_floor(actor->floor);
      // did we hit a mine?
      if (objtype == ITEM_MINE) {
        fall_down(actor);
        sfx_play(SND_HIT,0);
        vbright = 8; // flash
      } else {
        score = bcd_add(score, 1);
        sfx_play(SND_COIN,0);
      }
    }
  }
}

void move_player() {
  byte joy = pad_poll(0);
  move_actor(&actors[0], joy, true);
  pickup_object(&actors[0]);
}

byte iabs(int x) {
  return x >= 0 ? x : -x;
}

bool check_collision(Actor* a) {
  byte i;
  byte afloor = a->floor;
  // can't fall through basement
  if (afloor == 0) return false;
  // can't fall if already falling
  if (a->state == FALLING) return false;
  // iterate through entire list of actors
  for (i=1; i<MAX_ACTORS; i++) {
    Actor* b = &actors[i];
    // actors must be on same floor and within 8 pixels
    if (b->onscreen &&
        afloor == b->floor && 
        iabs(a->yy - b->yy) < 8 && 
        iabs(a->x - b->x) < 8) {
      return true;
    }
  }
  return false;
}

///

const char* RESCUE_TEXT = 
  "Is this a rescue?\n"
  "I am just hanging out\n"
  "on top of this building.\n"
  "Get lost!!!";

void type_message(const char* charptr) {
  char ch;
  byte x,y;
  x = 2;
  // compute message y position relative to scroll
  y = ROWS*3 + 39 - scroll_tile_y;
  while ((ch = *charptr++)) {
    while (y >= 60) y -= 60;
    if (ch == '\n') {
      x = 2;
      y++;
    } else {
      vrambuf_put(getntaddr(x, y), &ch, 1);
      x++;
    }
    // flush buffer and wait a few frames
    vrambuf_flush();
    delay(5);
  }
}

void rescue_scene() {
  // make player face to the left
  actors[0].dir = 1;
  actors[0].state = STANDING;
  refresh_sprites();
  type_message(RESCUE_TEXT);
  // wait 2 seconds
  delay(100);
}

void play_scene() {
  byte i;
  
  memset(actors, 0, sizeof(actors));
  actors[0].state = STANDING;
  actors[0].name = ACTOR_PLAYER;
  actors[0].pal = 3;
  actors[0].x = 64;
  actors[0].floor = 0;
  actors[0].yy = get_floor_yy(0);
  
  set_scroll_pixel_yy(0);
  draw_entire_stage();
  
  while (actors[0].floor != MAX_FLOORS-1) {
    //set_scroll_pixel_yy(scroll_pixel_yy+1);
    vrambuf_flush();
    refresh_sprites();
    move_player();
    // move all the actors
    for (i=1; i<MAX_ACTORS; i++) {
      move_actor(&actors[i], rand8(), false);
    }
    // see if the player hit another actor
    if (check_collision(&actors[0])) {
      fall_down(&actors[0]);
      sfx_play(SND_HIT,0);
      vbright = 8; // flash
    }
    if (vbright > 4) {
      pal_bright(--vbright);
    }
  }
  
  rescue_scene();
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x03,			// background color

  0x11,0x30,0x27, 0x0,	// ladders and pickups
  0x1c,0x20,0x2c, 0x0,	// floor blocks
  0x00,0x10,0x20, 0x0,
  0x06,0x16,0x26, 0x0,

  0x16,0x35,0x24, 0x0,	// enemy sprites
  0x00,0x37,0x25, 0x0,	// rescue person
  0x0d,0x2d,0x3a, 0x0,
  0x0d,0x27,0x2a	// player sprites
};

void setup_graphics() {
  ppu_off();
  oam_clear();
  pal_all(PALETTE);
  vram_adr(0x2000);
  vram_fill(CH_BLANK, 0x1000);
  vrambuf_clear();
  set_vram_update(updbuf);
  ppu_on_all();
}

void setup_sounds() {
  famitone_init(danger_streets_music_data);
  sfx_init(demo_sounds);
  nmi_set_callback(famitone_update);
}

void main() {
  setup_sounds();
  while (1) {
    setup_graphics();
    sfx_play(SND_START,0);
    make_floors();
    music_play(0);
    play_scene();
  }
}
