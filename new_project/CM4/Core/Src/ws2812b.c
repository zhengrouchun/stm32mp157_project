/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ws2812b.c
  * @brief   WS2812B 閻忣垰鐢す鍗炲З鐎圭偟骞囬敍灞煎▏閻?TIM2_CH3 PWM + DMA 鏉堟挸鍤稉銉︾壐閺冭泛绨妴?  ******************************************************************************
  */
/* USER CODE END Header */

#include "ws2812b.h"

#include <string.h>

#include "cmsis_os2.h"
#include "main.h"
#include "tim.h"

extern void rpmsg_send_string(const char *str);

#define WS2812B_BITS_PER_LED     24U
#define WS2812B_DMA_BUF_LEN      ((WS2812B_RESET_BITS * 2U) + (WS2812B_NUM_LEDS * WS2812B_BITS_PER_LED))
#define WS2812B_DMA_TIMEOUT_MS   20U

typedef struct
{
  uint8_t green;
  uint8_t red;
  uint8_t blue;
} Ws2812bPixel_t;

static Ws2812bPixel_t led_pixels[WS2812B_NUM_LEDS];
static uint32_t dma_buf[WS2812B_DMA_BUF_LEN];
static osSemaphoreId_t ws2812b_dma_done_sem;

static void WS2812B_BuildDmaBuffer(void);
static void WS2812B_PutByteToDmaBuffer(uint8_t value, uint32_t *offset);
static void WS2812B_ReportHalError(void);

void ws2812b_init(void)
{
  const osSemaphoreAttr_t sem_attr = {
    .name = "Ws2812DmaSem"
  };

  /* 鏉╂瑩鍣锋稉宥呭灥婵瀵?TIM2/PB10/DMA閿涘苯娲滄稉楦跨箹娴滄稖绁┃鎰暠 CubeMX 缂佺喍绔撮悽鐔稿灇閵?   * 妞瑰崬濮╅崣顏勫灡瀵ゅ搫鎮撳銉ヮ嚠鐠炩€宠嫙濞撳懐鈹栨潪顖欐閸嶅繒绀岀紓鎾崇摠閿涘矂浼╅崗宥囩壃閸?main.c 娑擃厼鍑￠張澶屾畱婢舵牞顔曢柊宥囩枂鏉堝湱鏅妴?*/
  ws2812b_dma_done_sem = osSemaphoreNew(1U, 0U, &sem_attr);
  memset(led_pixels, 0, sizeof(led_pixels));
  memset(dma_buf, 0, sizeof(dma_buf));
}

void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b)
{
  uint32_t i;

  for (i = 0U; i < WS2812B_NUM_LEDS; i++)
  {
    ws2812b_set_one((uint8_t)i, r, g, b);
  }
}

void ws2812b_set_one(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
  if (idx >= WS2812B_NUM_LEDS)
  {
    return;
  }

  /* WS2812B 閻ㄥ嫮鍤庢稉濠傚礂鐠侇喖娴愮€规矮璐?GRB 妞ゅ搫绨敍宀冣偓灞肩瑐鐏炲倷绗熼崝鈩冩纯閼奉亞鍔ч崷鎵暏 RGB 鐞涖劏鎻０婊嗗閵?   * 閸︺劑鈹嶉崝銊ㄧ珶閻ｅ苯鐣幋?RGB->GRB 鏉烆剚宕查敍灞藉讲娴犮儴顔€ LED 娴犺濮熼崣顏勫彠韫囧啠鈧粎瀛?缂?閽冩績鈧繄娈戞稉姘閸氼偂绠熼敍?   * 娑撳秵濡搁崗铚傜秼閻忣垳褰旈崡蹇氼唴濞夊嫭绱￠崚棰佹崲閸斺€崇湴閿涘苯鎮楃紒顓熸禌閹广垻浼呯敮锕€鐎烽崣閿嬫娑旂喐娲跨€硅妲楅弨鑸垫殐娣囶喗鏁奸懠鍐ㄦ纯閵?*/
  led_pixels[idx].green = g;
  led_pixels[idx].red = r;
  led_pixels[idx].blue = b;
}

void ws2812b_send(void)
{
  HAL_StatusTypeDef status;

  if (ws2812b_dma_done_sem == NULL)
  {
    WS2812B_ReportHalError();
    return;
  }

  WS2812B_BuildDmaBuffer();

  /* 閸氼垰濮╅崜宥呭帥閸嬫粍顒涙稉濠佺濞?PWM DMA閿涘本妲告稉杞扮啊闂冨弶顒涙稉濠佺鐢冪磽鐢憡婀€瑰本鍨氶弮璺哄晙濞嗏€虫儙閸?DMA閵?   * HAL 娴兼氨娣幎?TIM/DMA 閻樿埖鈧焦婧€閿涘苯鍘?Stop 閸?Start 閸欘垯浜掔拋鈺勭箾缂侇厼鎳℃禒銈呭徔閺堝鈥樼€规俺顢戞稉鐚寸幢
   * 鏉╂瑩鍣锋禒宥囧姧閸欘亣鐨熼悽?HAL API閿涘奔绗夐惄瀛樺复绾?TIM2 鐎靛嫬鐡ㄩ崳銊ｂ偓?*/
  (void)HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);

  status = HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_3, dma_buf, WS2812B_DMA_BUF_LEN);
  if (status != HAL_OK)
  {
    WS2812B_ReportHalError();
    return;
  }

  /* WS2812B 娑撯偓鐢勬殶閹诡喚瀹?12 * 24 * 1.25us閿涘苯鍟€閸旂姴顦叉担宥勭秵閻㈤潧閽╅敍宀冪箼鐏忓繋绨?20ms閵?   * 鐠佸墽鐤嗛張澶愭缁涘绶熼弮鍫曟？閼板奔绗夐弰顖涙娑斿懘妯嗘繅鐑囩礉閺勵垯璐熸禍?DMA/HAL 閻樿埖鈧礁绱撶敮鍛婃娑撳秵瀚嬪?LED 娴犺濮熼妴?*/
  if (osSemaphoreAcquire(ws2812b_dma_done_sem, WS2812B_DMA_TIMEOUT_MS) != osOK)
  {
    (void)HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
    WS2812B_ReportHalError();
  }
}

void ws2812b_clear(void)
{
  ws2812b_set_all(0U, 0U, 0U);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if ((htim != NULL) && (htim->Instance == TIM2))
  {
    (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_3);

    if (ws2812b_dma_done_sem != NULL)
    {
      (void)osSemaphoreRelease(ws2812b_dma_done_sem);
    }
  }
}

static void WS2812B_BuildDmaBuffer(void)
{
  uint32_t offset = 0U;
  uint32_t i;

  /* WS2812B 娌℃湁鐙珛甯уご锛屽彧闈犳€荤嚎淇濇寔浣庣數骞虫潵澶嶄綅鍐呴儴鎺ユ敹鐘舵€佹満銆?
   * 鍦?remoteproc 璋冭瘯杩囩▼涓紝濡傛灉涓婁竴甯ц鎵撴柇鎴栨椂搴忔浘缁忛敊璇紝鐏彔鍙兘鍋滃湪鈥滃崐鍖呪€濈姸鎬侊紱
   * 涓嬩竴甯т竴涓婃潵灏辨槸鏁版嵁浣嶆椂锛岀涓€棰楃伅浼氭妸瀹冭褰撴垚鏃у抚缁寘锛屽吀鍨嬬幇璞″氨鏄彧浜竴鍗娿€佺櫧鑹叉垨鍚庣画涓嶅彉鑹层€?
   * 鍥犳鏈┍鍔ㄥ湪鏁版嵁鍓嶅悗閮芥斁缃?reset 浣庣數骞筹細鍓?reset 娓呯┖鎺ユ敹鐘舵€侊紝鍚?reset 閿佸瓨鏈抚棰滆壊銆?
   * 鍏ㄨ繃绋嬩粛鐒堕€氳繃 TIM2 PWM + DMA 杈撳嚭 CCR=0锛屼笉鐩存帴鎿嶄綔 GPIO锛岀鍚堝彧浣跨敤 HAL 鐨勭害鏉熴€?*/
  while (offset < WS2812B_RESET_BITS)
  {
    dma_buf[offset] = 0U;
    offset++;
  }

  for (i = 0U; i < WS2812B_NUM_LEDS; i++)
  {
    WS2812B_PutByteToDmaBuffer(led_pixels[i].green, &offset);
    WS2812B_PutByteToDmaBuffer(led_pixels[i].red, &offset);
    WS2812B_PutByteToDmaBuffer(led_pixels[i].blue, &offset);
  }

  /* 褰撳墠 .ioc 涓?TIM2 杈撳嚭鏃堕挓鏄?209MHz锛孉RR=260 鏃朵竴浣嶅懆鏈熺害 1.249us銆?
   * 50 涓?CCR=0 鍛ㄦ湡绾?62us锛屾弧瓒?WS2812B 鏁版嵁鎵嬪唽涓浣嶄綆鐢靛钩澶т簬 50us 鐨勮姹傘€?*/
  while (offset < WS2812B_DMA_BUF_LEN)
  {
    dma_buf[offset] = 0U;
    offset++;
  }
}
static void WS2812B_PutByteToDmaBuffer(uint8_t value, uint32_t *offset)
{
  int8_t bit;

  for (bit = 7; bit >= 0; bit--)
  {
    if ((value & (uint8_t)(1U << (uint8_t)bit)) != 0U)
    {
      dma_buf[*offset] = WS2812B_BIT_1_CCR;
    }
    else
    {
      dma_buf[*offset] = WS2812B_BIT_0_CCR;
    }

    (*offset)++;
  }
}

static void WS2812B_ReportHalError(void)
{
  /* 娴ｇ姴鍑＄憰浣圭湴閸樼粯甯€ PD6 閻樿埖鈧胶浼呴崝鐔诲厴閿涘苯娲滃銈堢箹闁插奔绗夐崘宥呬粵閺堫剙婀?GPIO 閹躲儵鏁婇梻顏嗗剨閵?   * 婵″倹鐏?RPMsg 瀹歌尙绮″铏圭彌閿涘苯鏁栭柌蹇斿Ω娑撱儵鍣哥涵顑挎闁挎瑨顕ゆ稉濠冨Г缂?A7閿涙稑顩ч弸婊勵劃閺冨爼鎽肩捄顖涙弓鐏忚京鍗庨敍?   * rpmsg_send_string 閸愬懘鍎存导姘舵饯姒涙绻戦崶鐑囩礉闁灝鍘ら柨娆掝嚖婢跺嫮鎮婇崘宥呯穿閸忋儵妯嗘繅鐐偓?*/
  rpmsg_send_string("EVENT:ERR_HAL");
}
