#include <algorithm>
#include <cmath>

#include "gamemapcore.h"

namespace gamemap {

Vec2 steerTarget(Vec2 center, const SteerOffsets &offsets, SteerKeys keys, double scale)
{
    Vec2 offset;
    if (keys.up) {
        offset.y -= offsets.up;
    }
    if (keys.down) {
        offset.y += offsets.down;
    }
    if (keys.left) {
        offset.x -= offsets.left;
    }
    if (keys.right) {
        offset.x += offsets.right;
    }
    return center + offset * scale;
}

std::vector<Vec2> interpolate(Vec2 from, Vec2 to, double stepLength)
{
    std::vector<Vec2> points;
    Vec2 d = to - from;
    double distance = std::max(std::fabs(d.x), std::fabs(d.y));
    if (stepLength <= 0.0 || distance <= stepLength) {
        points.push_back(to);
        return points;
    }

    int steps = static_cast<int>(std::ceil(distance / stepLength));
    points.reserve(steps);
    for (int i = 1; i < steps; ++i) {
        points.push_back(from + d * (static_cast<double>(i) / steps));
    }
    points.push_back(to);
    return points;
}

std::vector<TouchOp> ViewTracker::move(Vec2 delta)
{
    std::vector<TouchOp> ops;
    if (m_waitingRecenter) {
        m_carry = m_carry + delta;
        return ops;
    }

    if (!m_touching) {
        m_pos = m_start;
        m_touching = true;
        ops.push_back({ TouchOp::Down, m_pos });
    }

    Vec2 target = m_pos + delta;
    if (m_bounds.contains(target)) {
        m_pos = target;
        ops.push_back({ TouchOp::Move, m_pos });
        return ops;
    }

    Vec2 edge = clipToBounds(m_pos, target);
    ops.push_back({ TouchOp::Move, edge });
    ops.push_back({ TouchOp::Up, edge });
    m_carry = target - edge;
    m_pos = edge;
    m_touching = false;
    m_waitingRecenter = true;
    return ops;
}

std::vector<TouchOp> ViewTracker::resume()
{
    std::vector<TouchOp> ops;
    if (!m_waitingRecenter) {
        return ops;
    }
    m_waitingRecenter = false;

    // Never carry more than the room available in one drag, otherwise a huge
    // flick would keep re-centering forever.
    double maxX = (m_bounds.right - m_bounds.left) / 2.0;
    double maxY = (m_bounds.bottom - m_bounds.top) / 2.0;
    Vec2 carry(std::max(-maxX, std::min(m_carry.x, maxX)), std::max(-maxY, std::min(m_carry.y, maxY)));
    m_carry = Vec2();

    ops = move(carry);
    return ops;
}

std::vector<TouchOp> ViewTracker::release()
{
    std::vector<TouchOp> ops;
    if (m_touching) {
        ops.push_back({ TouchOp::Up, m_pos });
    }
    m_touching = false;
    m_waitingRecenter = false;
    m_carry = Vec2();
    return ops;
}

Vec2 ViewTracker::clipToBounds(Vec2 from, Vec2 to) const
{
    // Largest t in [0, 1] such that from + (to - from) * t stays inside bounds.
    Vec2 d = to - from;
    double t = 1.0;
    if (d.x > 0.0) {
        t = std::min(t, (m_bounds.right - from.x) / d.x);
    } else if (d.x < 0.0) {
        t = std::min(t, (m_bounds.left - from.x) / d.x);
    }
    if (d.y > 0.0) {
        t = std::min(t, (m_bounds.bottom - from.y) / d.y);
    } else if (d.y < 0.0) {
        t = std::min(t, (m_bounds.top - from.y) / d.y);
    }
    t = std::max(0.0, t);
    return from + d * t;
}

} // namespace gamemap
