#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include "lib/GfxRenderer/GlyphBitmap.h"

namespace {
constexpr int PANEL_WIDTH = 40;
constexpr int PANEL_HEIGHT = 32;
constexpr int STRIDE = PANEL_WIDTH / 8;

using glyphBitmap::Plane;

// Logical placements used by the renderer: unrotated text, and side-button
// labels rotated 90 degrees clockwise (glyph x runs up, glyph y runs right).
constexpr std::array<std::array<int, 4>, 2> AXES = {{{1, 0, 0, 1}, {0, -1, 1, 0}}};

std::pair<int, int> physical(int orientation, int x, int y) {
  switch (orientation) {
    case 0:
      return {y, PANEL_HEIGHT - 1 - x};
    case 1:
      return {PANEL_WIDTH - 1 - x, PANEL_HEIGHT - 1 - y};
    case 2:
      return {PANEL_WIDTH - 1 - y, x};
    default:
      return {x, y};
  }
}

// Logical rectangle the renderer clips to; the glyph-local clip is derived from it.
struct Rect {
  int left;
  int top;
  int right;
  int bottom;
};

void compare(int orientation, int rotation, bool twoBit, Plane plane, bool state, int width, int height, int x, int y,
             int originY, int rows, Rect clip) {
  SCOPED_TRACE(::testing::Message() << orientation << ',' << rotation << ',' << twoBit << ',' << static_cast<int>(plane)
                                    << ',' << state << " size=" << width << 'x' << height << " at=" << x << ',' << y
                                    << " band=" << originY << ',' << rows);
  const auto [dxX, dxY, dyX, dyY] = AXES[rotation];
  const glyphBitmap::Frame logical{x, y, dxX, dxY, dyX, dyY};
  std::vector<uint8_t> bitmap((width * height * (twoBit ? 2 : 1) + 7) / 8);
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = static_cast<uint8_t>(i * 73 + 0x1b);
  // Nonuniform background and guards verify transparent pixels and all off-band bytes.
  std::vector<uint8_t> expected(rows * STRIDE + 32);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  for (int gy = 0; gy < height; ++gy) {
    for (int gx = 0; gx < width; ++gx) {
      const int lx = x + gx * dxX + gy * dyX;
      const int ly = y + gx * dxY + gy * dyY;
      if (lx < clip.left || ly < clip.top || lx >= clip.right || ly >= clip.bottom) continue;
      const auto [px, py] = physical(orientation, lx, ly);
      if (px < 0 || px >= PANEL_WIDTH || py < originY || py >= originY + rows) continue;
      const int source = gy * width + gx;
      bool draw = false;
      bool black = state;
      if (twoBit) {
        const int value = 3 - ((bitmap[source / 4] >> ((3 - source % 4) * 2)) & 3);
        if (plane == Plane::BW) draw = value < 3;
        if (plane == Plane::GrayLSB) draw = value == 1;
        if (plane == Plane::GrayMSB) draw = value == 1 || value == 2;
        if (plane != Plane::BW) black = false;
      } else {
        draw = (bitmap[source / 8] >> (7 - source % 8)) & 1;
      }
      if (!draw) continue;
      auto& byte = expected[16 + (py - originY) * STRIDE + px / 8];
      if (black)
        byte &= static_cast<uint8_t>(~(0x80 >> (px % 8)));
      else
        byte |= 0x80 >> (px % 8);
    }
  }

  glyphBitmap::Clip local{0, 0, width, height};
  glyphBitmap::clipToRect(logical, clip.left, clip.top, clip.right, clip.bottom, local);

  const auto [px, py] = physical(orientation, x, y);
  const auto [xx, xy] = physical(orientation, x + dxX, y + dxY);
  const auto [yx, yy] = physical(orientation, x + dyX, y + dyY);
  const glyphBitmap::Target target{
      actual.data() + 16, PANEL_WIDTH, STRIDE, originY, rows, {px, py, xx - px, xy - py, yx - px, yy - py}};
  glyphBitmap::draw(bitmap.data(), width, height, twoBit, plane, state, target, local);
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(GlyphBitmap, MatchesPerPixelReferenceAcrossOrientationsRotationsPlanesAndClipping) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 2; ++rotation) {
      for (bool twoBit : {false, true}) {
        for (Plane plane : {Plane::BW, Plane::GrayLSB, Plane::GrayMSB}) {
          for (bool state : {false, true}) {
            for (int width : {1, 3, 7, 16, 31}) {
              for (const auto [x, y] : {std::pair{-5, -3}, std::pair{0, 0}, std::pair{9, 11}, std::pair{29, 31}}) {
                for (const auto [origin, rows] :
                     {std::pair{0, 32}, std::pair{0, 7}, std::pair{7, 11}, std::pair{28, 4}}) {
                  compare(orientation, rotation, twoBit, plane, state, width, 13, x, y, origin, rows,
                          {-20, -20, 60, 60});
                  compare(orientation, rotation, twoBit, plane, state, width, 13, x, y, origin, rows, {2, 3, 18, 20});
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST(GlyphBitmap, EmptyAndFullyClippedGlyphsDoNotWrite) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 2; ++rotation) {
      compare(orientation, rotation, true, Plane::BW, true, 0, 0, 0, 0, 0, 32, {0, 0, 40, 32});
      compare(orientation, rotation, true, Plane::BW, true, 11, 9, 100, 100, 0, 32, {0, 0, 40, 32});
      compare(orientation, rotation, true, Plane::BW, true, 11, 9, 0, 0, 0, 32, {12, 10, 15, 20});
    }
  }
}

namespace {
// Pseudo-random 2bpp glyph so that every value 0..3 occurs.
std::vector<uint8_t> patternBitmap(int width, int height, bool twoBit) {
  std::vector<uint8_t> bitmap((width * height * (twoBit ? 2 : 1) + 7) / 8 + 1);
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = static_cast<uint8_t>(i * 91 + 0x2d);
  return bitmap;
}
}  // namespace

// One pass with a second buffer paints the LSB plane into `buffer` and the MSB plane into
// `buffer2`, exactly as two separate single-plane passes would.
TEST(GlyphBitmap, DualPlaneMatchesSeparateLsbAndMsbPasses) {
  for (const auto& axes : AXES) {
    for (bool twoBit : {false, true}) {
      for (const auto [x, y] : {std::pair{-4, -2}, std::pair{3, 5}, std::pair{30, 20}}) {
        const int width = 13;
        const int height = 11;
        const auto bitmap = patternBitmap(width, height, twoBit);
        const glyphBitmap::Frame frame{x, y, axes[0], axes[1], axes[2], axes[3]};
        glyphBitmap::Clip clip{0, 0, width, height};
        std::vector<uint8_t> lsb(PANEL_HEIGHT * STRIDE, 0), msb(PANEL_HEIGHT * STRIDE, 0);
        auto expectedLsb = lsb;
        auto expectedMsb = msb;
        glyphBitmap::draw(bitmap.data(), width, height, twoBit, Plane::GrayLSB, false,
                          {expectedLsb.data(), PANEL_WIDTH, STRIDE, 0, PANEL_HEIGHT, frame}, clip);
        glyphBitmap::draw(bitmap.data(), width, height, twoBit, Plane::GrayMSB, false,
                          {expectedMsb.data(), PANEL_WIDTH, STRIDE, 0, PANEL_HEIGHT, frame}, clip);
        glyphBitmap::draw(bitmap.data(), width, height, twoBit, Plane::GrayLSB, false,
                          {lsb.data(), PANEL_WIDTH, STRIDE, 0, PANEL_HEIGHT, frame, msb.data()}, clip);
        EXPECT_EQ(expectedLsb, lsb);
        EXPECT_EQ(expectedMsb, msb);
      }
    }
  }
}

// Absolute levels: both planes start white (0xFF); light ink clears the LSB bit, dark ink the
// MSB bit, black ink both. White (value 0) leaves the pixel alone.
TEST(GlyphBitmap, AbsoluteDualPlaneClearsBitsPerLevel) {
  const int width = 9;
  const int height = 7;
  const auto bitmap = patternBitmap(width, height, true);
  const int fx = 5;
  const int fy = 3;
  std::vector<uint8_t> lsb(PANEL_HEIGHT * STRIDE, 0xFF), msb(PANEL_HEIGHT * STRIDE, 0xFF);
  auto expectedLsb = lsb;
  auto expectedMsb = msb;
  for (int gy = 0; gy < height; ++gy) {
    for (int gx = 0; gx < width; ++gx) {
      const int source = gy * width + gx;
      const int raw = (bitmap[source / 4] >> ((3 - source % 4) * 2)) & 3;  // 0 white, 1 light, 2 dark, 3 black
      const int px = fx + gx;
      const int py = fy + gy;
      const uint8_t mask = static_cast<uint8_t>(0x80 >> (px % 8));
      if (raw == 1 || raw == 3) expectedLsb[py * STRIDE + px / 8] &= static_cast<uint8_t>(~mask);
      if (raw == 2 || raw == 3) expectedMsb[py * STRIDE + px / 8] &= static_cast<uint8_t>(~mask);
    }
  }
  glyphBitmap::Target target{lsb.data(), PANEL_WIDTH, STRIDE, 0, PANEL_HEIGHT, {fx, fy, 1, 0, 0, 1}, msb.data(), true};
  glyphBitmap::draw(bitmap.data(), width, height, true, Plane::GrayLSB, false, target, {0, 0, width, height});
  EXPECT_EQ(expectedLsb, lsb);
  EXPECT_EQ(expectedMsb, msb);
}
