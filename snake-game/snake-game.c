#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "altera_up_avalon_audio.h"
#include "altera_up_avalon_character_lcd.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_avalon_parallel_port.h"
#include "sys/alt_stdio.h"

#define BUF_THRESHOLD 96 // 75% of 128 word buffer
#define DISPLAY_W 320
#define DISPLAY_H 240
#define CELL_WH 10
#define COLOR_BG 0x4a8b
#define COLOR_SNAKE_BODY 0x3522
#define COLOR_SNAKE_HEAD 0x6ec3
#define COLOR_FEED 0xe882

alt_up_parallel_port_dev *KEY_dev;
alt_up_char_buffer_dev *char_buffer_dev;
alt_up_pixel_buffer_dma_dev *pixel_buffer_dev;

/**
 * 座標の構造体
 */
typedef struct
{
  int x, y;
} pos;

/**
 * 蛇の構造体
 */
typedef struct
{
  pos cell[800];
  int len;
} snake;

/**
 * 1マス分の箱を描画
 * @param[in] x  最大 DISPLAY_W / CELL_WH
 * @param[in] y  最大 DISPLAY_H / CELL_WH
 */
void drawCell(int x, int y, int color)
{
  alt_up_pixel_buffer_dma_draw_box(pixel_buffer_dev, x * CELL_WH, y * CELL_WH, x * CELL_WH + CELL_WH, y * CELL_WH + CELL_WH, color, 0);
}

/**
 * 1マス分の箱をクリア
 * @param[in] y  最大 DISPLAY_H / CELL_WH
 */
void clearCell(int x, int y, int bgColor)
{
  alt_up_pixel_buffer_dma_draw_box(pixel_buffer_dev, x * CELL_WH, y * CELL_WH, x * CELL_WH + CELL_WH, y * CELL_WH + CELL_WH, bgColor, 0);
}

/**
 * 背景を描画
 */
void drawBackGround()
{
  int i, j;
  for (i = 0; i < DISPLAY_W; i++)
  {
    for (j = 0; j < DISPLAY_H; j++)
    {
      drawCell(i, j, COLOR_BG);
    }
  }
}

/**
 * 画面の枠線を描画
 */
void drawFrame()
{
  alt_up_pixel_buffer_dma_draw_line(pixel_buffer_dev, 0, 0, DISPLAY_W - 1, 0, 0xffff, 0);                         // 上
  alt_up_pixel_buffer_dma_draw_line(pixel_buffer_dev, 0, DISPLAY_H - 1, DISPLAY_W - 1, DISPLAY_H - 1, 0xffff, 0); // 下
  alt_up_pixel_buffer_dma_draw_line(pixel_buffer_dev, 0, 0, 0, DISPLAY_H - 1, 0xffff, 0);                         // 左
  alt_up_pixel_buffer_dma_draw_line(pixel_buffer_dev, DISPLAY_W - 1, 0, DISPLAY_W - 1, DISPLAY_H - 1, 0xffff, 0); // 右
}

/**
 * 蛇を描画
 */
void drawSnake(snake snake)
{
  int i;
  for (i = 0; i < snake.len - 1; i++)
  {
    if (i == 0)
    {
      drawCell(snake.cell[i].x, snake.cell[i].y, COLOR_SNAKE_HEAD);
    }
    else
    {
      drawCell(snake.cell[i].x, snake.cell[i].y, COLOR_SNAKE_BODY);
    }
  }
}

/**
 * 蛇をクリア
 */
void clearSnake(snake snake)
{
  int i;
  for (i = 0; i < snake.len - 1; i++)
  {
    int x = snake.cell[i].x;
    int y = snake.cell[i].y;
    drawCell(x, y, COLOR_BG);
  }
}

/**
 * 餌をクリア
 */
void clearFeed(pos feed)
{
  drawCell(feed.x, feed.y, COLOR_BG);
}

/**
 * 餌を描画
 */
void drawFeed(pos feed)
{
  drawCell(feed.x, feed.y, COLOR_FEED);
}

/**
 * ゲームオーバー画面を描画
 */
void drawGameOver(int score)
{
  alt_up_pixel_buffer_dma_clear_screen(pixel_buffer_dev, 0);
  alt_up_char_buffer_clear(char_buffer_dev);

  char message[20] = {'\0'};
  sprintf(message, "Score: %d", score);
  alt_up_char_buffer_string(char_buffer_dev, "Game Over\0", 80 / 2 - 4, 60 / 2); // 最大80x60
  alt_up_char_buffer_string(char_buffer_dev, message, 80 / 2 - 5, 60 / 2 + 3);
}

/**
 * スコア表示
 */
void printScore(int score)
{
  char message[20] = {'\0'};
  sprintf(message, "Score: %d", score);
  alt_up_char_buffer_string(char_buffer_dev, message, 1, 2);
}

/**
 * @param[in] direction 蛇の進行方向. 右:1, 上:2, :4, 左:8
 * @return    snake     更新前の蛇の最後尾cellの座標
 */
pos moveSnake(snake *snake, int direction)
{
  // 更新前の最後尾cellの座標を保持
  pos tail = snake->cell[snake->len - 1];
  // 頭以外のcellについて, 座標を更新
  int i;
  for (i = snake->len - 1; i >= 1; i--)
  {
    snake->cell[i] = snake->cell[i - 1];
  }

  // 頭の座標を更新
  switch (direction)
  {
  case 1:
    snake->cell[0].x++;
    break;
  case 2:
    snake->cell[0].y--;
    break;
  case 4:
    snake->cell[0].y++;
    break;
  case 8:
    snake->cell[0].x--;
    break;
  }

  return tail;
}

/**
 * param[in] tail 追加される最後尾のcellの座標
 */
void growSnake(snake *snake, pos tail)
{
  snake->cell[snake->len] = tail;

  snake->len++;
}

/**
 * @return ゲームオーバーなら1, それ以外は0
 */
int isGameOver(snake snake)
{
  if (snake.cell[0].x < 0 || snake.cell[0].y < 0)
  {
    return 1;
  }

  if (snake.cell[0].x >= DISPLAY_W / CELL_WH || snake.cell[0].y >= DISPLAY_H / CELL_WH)
  {
    return 1;
  }

  // 頭の座標が体の座標のどれかと一致していたらゲームオーバー
  int i;
  for (i = 1; i < snake.len; i++)
  {
    if (snake.cell[0].x == snake.cell[i].x && snake.cell[0].y == snake.cell[i].y)
    {
      return 1;
    }
  }

  return 0;
}

/**
 * @return 餌があったら1, それ以外は0
 */
int isGetFeed(snake snake, pos feed)
{
  if (snake.cell[0].x == feed.x && snake.cell[0].y == feed.y)
  {
    return 1;
  }

  return 0;
}

/**
 * @return プッシュボタンのキーを受け取る処理
 */
int getInputKey(alt_up_parallel_port_dev *KEY_dev)
{
  int KEY_value = alt_up_parallel_port_read_data(KEY_dev);
  int xor = 0x0 ^ KEY_value;
  int hd = 0; // 0x0とKEY_valueのハミング距離
  while (xor)
  {
    xor &= xor-1;
    hd++;
  }
  if (hd > 1)
  { // 同時押しは無効
    return 0;
  }

  return KEY_value;
}

/**
 * @return feed ランダムな餌の座標
 */
pos newFeed()
{
  srand(time(NULL));
  int x = rand() % (DISPLAY_W / CELL_WH);
  int y = rand() % (DISPLAY_H / CELL_WH);
  pos feed = {x, y};

  return feed;
}

int main(void)
{
  // ピクセルバッファをオープン
  pixel_buffer_dev = alt_up_pixel_buffer_dma_open_dev("/dev/VGA_Pixel_Buffer");

  // キャラクタバッファをオープン
  char_buffer_dev = alt_up_char_buffer_open_dev("/dev/VGA_Char_Buffer");

  // プッシュボタンのパラレルポートをオープン
  KEY_dev = alt_up_parallel_port_open_dev("/dev/Pushbuttons");

  // キャラクタバッファをクリア
  alt_up_char_buffer_clear(char_buffer_dev);

  // ピクセルバッファをクリア
  alt_up_pixel_buffer_dma_clear_screen(pixel_buffer_dev, 0);

  drawBackGround();

  int direction = 2;
  int score = 0;

  snake snake = {
      {{14, 12},
       {13, 12},
       {12, 12},
       {11, 12}},
      4};

  pos tail;
  pos feed = newFeed();

  while (1)
  {
    drawFeed(feed);
    drawSnake(snake);
    drawFrame();
    printScore(score);

    usleep(200000);

    int KEY_value = getInputKey(KEY_dev);
    if (KEY_value)
    {
      if (!(direction == 1 && KEY_value == 8 || direction == 8 && KEY_value == 1 || direction == 2 && KEY_value == 4 || direction == 4 && KEY_value == 2))
      {
        direction = KEY_value;
      }
    }

    clearSnake(snake);
    tail = moveSnake(&snake, direction);

    if (isGameOver(snake))
    {
      break;
    }

    if (isGetFeed(snake, feed))
    {
      growSnake(&snake, tail);
      clearFeed(feed);
      feed = newFeed();
      score = score + 100;
    }
  }

  drawGameOver(score);

  return 0;
}
