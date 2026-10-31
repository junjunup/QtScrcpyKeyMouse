#ifndef GAMEMAPCORE_H
#define GAMEMAPCORE_H

// Qt-free core of the game key mapping: steer wheel targets / interpolation
// and the mouse-look (view) finger tracker. All positions are fractions of
// the device frame (0.0 ~ 1.0), so this file can be unit tested without Qt.

#include <vector>

namespace gamemap {

struct Vec2
{
    Vec2(double x = 0.0, double y = 0.0) : x(x), y(y) {}
    double x;
    double y;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator*(Vec2 a, double k) { return { a.x * k, a.y * k }; }

struct Rect
{
    Rect(double left = 0.05, double top = 0.05, double right = 0.95, double bottom = 0.95) : left(left), top(top), right(right), bottom(bottom) {}
    double left;
    double top;
    double right;
    double bottom;

    bool contains(Vec2 p) const { return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom; }
};

// -------- steer wheel --------

struct SteerOffsets
{
    SteerOffsets(double up = 0.0, double down = 0.0, double left = 0.0, double right = 0.0) : up(up), down(down), left(left), right(right) {}
    double up;
    double down;
    double left;
    double right;
};

struct SteerKeys
{
    SteerKeys(bool up = false, bool down = false, bool left = false, bool right = false) : up(up), down(down), left(left), right(right) {}
    bool up;
    bool down;
    bool left;
    bool right;

    int pressedCount() const { return int(up) + int(down) + int(left) + int(right); }
};

// Target finger position for the pressed direction keys. scale multiplies every
// offset (e.g. 0.5 while a "walk" modifier is held, 1.3 to push into auto-sprint).
Vec2 steerTarget(Vec2 center, const SteerOffsets &offsets, SteerKeys keys, double scale = 1.0);

// Points of a straight drag from `from` to `to` (exclusive of `from`, inclusive
// of `to`), spaced by at most `stepLength`. stepLength <= 0 jumps directly.
std::vector<Vec2> interpolate(Vec2 from, Vec2 to, double stepLength);

// -------- mouse look --------

struct TouchOp
{
    enum Type
    {
        Down,
        Move,
        Up
    };
    Type type;
    Vec2 pos;
};

// Tracks the single finger that drags the camera. When the finger would leave
// `bounds` it is moved to the boundary, lifted, and the remaining distance is
// carried over to the next touch-down at the start position, so no mouse
// movement is lost across a re-center.
class ViewTracker
{
public:
    void setStart(Vec2 start) { m_start = start; }
    void setBounds(Rect bounds) { m_bounds = bounds; }
    Vec2 start() const { return m_start; }

    bool touching() const { return m_touching; }
    bool waitingRecenter() const { return m_waitingRecenter; }
    Vec2 position() const { return m_pos; }

    // delta is in frame fractions. After a re-center (waitingRecenter() == true)
    // the caller should call resume() once its re-center delay has elapsed.
    std::vector<TouchOp> move(Vec2 delta);
    std::vector<TouchOp> resume();
    std::vector<TouchOp> release();

private:
    Vec2 clipToBounds(Vec2 from, Vec2 to) const;

    Vec2 m_start;
    Rect m_bounds;
    Vec2 m_pos;
    Vec2 m_carry;
    bool m_touching = false;
    bool m_waitingRecenter = false;
};

} // namespace gamemap

#endif // GAMEMAPCORE_H
