// Standalone tests for gamemapcore (no Qt needed):
//   c++ -std=c++17 -I src/device/controller/inputconvert/keymap \
//       test/gamemapcore_test.cpp src/device/controller/inputconvert/keymap/gamemapcore.cpp -o gamemapcore_test
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gamemapcore.h"

using namespace gamemap;

static int s_failed = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++s_failed;                                                   \
        }                                                                 \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
static bool near(Vec2 a, Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); }

static void testSteerTarget()
{
    Vec2 c { 0.16, 0.75 };
    SteerOffsets o { 0.27, 0.2, 0.1, 0.1 };

    CHECK(near(steerTarget(c, o, { true, false, false, false }), { 0.16, 0.48 }));
    CHECK(near(steerTarget(c, o, { false, true, false, true }), { 0.26, 0.95 }));
    // opposite keys cancel out
    CHECK(near(steerTarget(c, o, { false, false, true, true }), c));
    // walk modifier halves the push
    CHECK(near(steerTarget(c, o, { true, false, false, false }, 0.5), { 0.16, 0.615 }));
    CHECK(near(steerTarget(c, o, {}), c));
}

static void testInterpolate()
{
    auto pts = interpolate({ 0.0, 0.0 }, { 0.1, 0.05 }, 0.01);
    CHECK(pts.size() == 10);
    CHECK(near(pts.back(), { 0.1, 0.05 }));
    CHECK(near(pts.front(), { 0.01, 0.005 }));
    // evenly spaced
    for (size_t i = 1; i < pts.size(); ++i) {
        CHECK(near(pts[i].x - pts[i - 1].x, 0.01));
    }

    // zero step or tiny distance jumps directly
    CHECK(interpolate({ 0, 0 }, { 0.3, 0.3 }, 0.0).size() == 1);
    CHECK(interpolate({ 0, 0 }, { 0.005, 0 }, 0.01).size() == 1);
    // no movement still yields the target once
    CHECK(near(interpolate({ 0.2, 0.2 }, { 0.2, 0.2 }, 0.01).front(), { 0.2, 0.2 }));
}

static void testViewTrackerBasic()
{
    ViewTracker t;
    t.setStart({ 0.5, 0.5 });
    t.setBounds({ 0.1, 0.1, 0.9, 0.9 });

    auto ops = t.move({ 0.1, 0.0 });
    CHECK(ops.size() == 2);
    CHECK(ops[0].type == TouchOp::Down && near(ops[0].pos, { 0.5, 0.5 }));
    CHECK(ops[1].type == TouchOp::Move && near(ops[1].pos, { 0.6, 0.5 }));
    CHECK(t.touching());

    ops = t.move({ -0.05, 0.1 });
    CHECK(ops.size() == 1 && ops[0].type == TouchOp::Move && near(ops[0].pos, { 0.55, 0.6 }));

    ops = t.release();
    CHECK(ops.size() == 1 && ops[0].type == TouchOp::Up && near(ops[0].pos, { 0.55, 0.6 }));
    CHECK(!t.touching());
    CHECK(t.release().empty());
}

static void testViewTrackerRecenterKeepsDistance()
{
    ViewTracker t;
    t.setStart({ 0.5, 0.5 });
    t.setBounds({ 0.1, 0.1, 0.9, 0.9 });

    t.move({ 0.3, 0.0 }); // at 0.8
    auto ops = t.move({ 0.2, 0.0 }); // wants 1.0 -> clipped to 0.9
    CHECK(ops.size() == 2);
    CHECK(ops[0].type == TouchOp::Move && near(ops[0].pos, { 0.9, 0.5 }));
    CHECK(ops[1].type == TouchOp::Up && near(ops[1].pos, { 0.9, 0.5 }));
    CHECK(t.waitingRecenter());
    CHECK(!t.touching());

    // movement while waiting is accumulated, not dropped
    CHECK(t.move({ 0.05, 0.02 }).empty());

    ops = t.resume();
    CHECK(ops.size() == 2);
    CHECK(ops[0].type == TouchOp::Down && near(ops[0].pos, { 0.5, 0.5 }));
    // carried 0.1 (clipped remainder) + 0.05 + y 0.02
    CHECK(ops[1].type == TouchOp::Move && near(ops[1].pos, { 0.65, 0.52 }));
    CHECK(!t.waitingRecenter());
}

static void testViewTrackerDiagonalClip()
{
    ViewTracker t;
    t.setStart({ 0.5, 0.5 });
    t.setBounds({ 0.1, 0.1, 0.9, 0.9 });

    // y hits the bottom first: t = 0.4 / 0.8 = 0.5
    auto ops = t.move({ 0.2, 0.8 });
    CHECK(ops.size() == 3); // Down, Move(edge), Up
    CHECK(near(ops[1].pos, { 0.6, 0.9 }));
    CHECK(ops[2].type == TouchOp::Up);
}

static void testViewTrackerHugeFlickIsBounded()
{
    ViewTracker t;
    t.setStart({ 0.5, 0.5 });
    t.setBounds({ 0.1, 0.1, 0.9, 0.9 });

    t.move({ 5.0, 0.0 });
    CHECK(t.waitingRecenter());
    auto ops = t.resume();
    // carry is capped to half the bounds width, so the resumed drag fits
    CHECK(ops.size() == 2);
    CHECK(ops[1].type == TouchOp::Move && near(ops[1].pos, { 0.9, 0.5 }));
    CHECK(!t.waitingRecenter());
}

static void testReleaseWhileWaiting()
{
    ViewTracker t;
    t.setStart({ 0.5, 0.5 });
    t.setBounds({ 0.1, 0.1, 0.9, 0.9 });
    t.move({ 1.0, 0.0 });
    CHECK(t.waitingRecenter());
    CHECK(t.release().empty()); // finger is already up
    CHECK(!t.waitingRecenter());
    CHECK(t.resume().empty());
}

int main()
{
    testSteerTarget();
    testInterpolate();
    testViewTrackerBasic();
    testViewTrackerRecenterKeepsDistance();
    testViewTrackerDiagonalClip();
    testViewTrackerHugeFlickIsBounded();
    testReleaseWhileWaiting();
    if (s_failed) {
        std::printf("%d check(s) failed\n", s_failed);
        return EXIT_FAILURE;
    }
    std::printf("all gamemapcore tests passed\n");
    return EXIT_SUCCESS;
}
