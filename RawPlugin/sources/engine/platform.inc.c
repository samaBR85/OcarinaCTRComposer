static Handle   thread;
static Handle   onProcessExitEvent, resumeExitEvent;
#define PLG_STACK_SIZE 0x4000            // 16KB (printf + hid + deep calls need room)
static u8       stack[PLG_STACK_SIZE] ALIGN(8);

// Our worker thread is spawned with a raw svcCreateThread, so its libctru
// ThreadVars (TLS+0) is never initialized. Newlib/hid/fs read magic@0 and
// reent@0x8 from there and svcBreak on a bad magic. Seed it once.
extern struct _reent *_impure_ptr;
static void InitThreadVars(void)
{
    volatile u32 *tv = (volatile u32 *)getThreadLocalStorage();
    tv[0] = 0x21545624;            // THREADVARS_MAGIC
    tv[1] = 0;                     // thread_ptr
    tv[2] = (u32)_impure_ptr;      // reent  (global newlib reentrancy)
    tv[3] = 0;                     // tls_tp
    tv[4] = 0;                     // fs_magic (0 = use the global fs session)
}

// --- Direct game-memory access (plugin runs inside the game process) ---
static inline void  W8(u32 a, u8 v)   { *(volatile u8  *)a = v; }
static inline void  W16(u32 a, u16 v) { *(volatile u16 *)a = v; }
static inline void  W32(u32 a, u32 v) { *(volatile u32 *)a = v; }
static inline u8    R8(u32 a)          { return *(volatile u8  *)a; }
static inline u16   R16(u32 a)         { return *(volatile u16 *)a; }
static inline u32   R32(u32 a)         { return *(volatile u32 *)a; }

#define G_BASE  0x005A2E3C   // base pointer to Link actor (OoT3D EUR)

// ===================== LCD registers =====================
#define LCD_TOP     0x10400400
#define LCD_BOT     0x10400500
#define LCD_FBA1    0x68
#define LCD_FBA2    0x6C
#define LCD_FORMAT  0x70
#define LCD_SELECT  0x78
#define LCD_STRIDE  0x90
#define TOP_W  400
#define TOP_H  240
#define BOT_W  320
#define BOT_H  240
