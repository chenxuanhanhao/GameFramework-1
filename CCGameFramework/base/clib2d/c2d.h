﻿//
// Project: clib2d
// Created by bajdcc
//

#ifndef CLIB2D_C2D_H
#define CLIB2D_C2D_H

#include <limits>
#include <string>
#include <cmath>
#include <chrono>
#include "../ui/gdi/gdi.h"

#define FPS 30
#define GRAVITY -9.8
#define FRAME_SPAN (1.0 / FPS)
#define SPEED_UP 5
#define COLLISION_ITERATIONS 10
#define EPSILON 1e-6
#define EPSILON_FORCE 1e-4
#define EPSILON_V 1e-4
#define EPSILON_ANGLE_V 1e-4
#define COLL_NORMAL_SCALE 1
#define COLL_TANGENT_SCALE 1
#define COLL_BIAS 0.75
#define COLL_CIR_POLY_BIAS 2e-4
#define COLL_CO 0.1
#define ENABLE_SLEEP 1
#define CIRCLE_N 60
#define PI2 (2 * M_PI)

namespace clib {

    using decimal = double; // 浮点类型
    static const auto inf = std::numeric_limits<decimal>::infinity();

    // 浮点带倒数
    struct decimal_inv {
        decimal value{ 0 }, inv{ 0 };

        explicit decimal_inv(decimal v);

        void set(decimal v);
    };

    // 浮点带平方
    struct decimal_square {
        decimal value{ 0 }, square{ 0 };

        explicit decimal_square(decimal v);

        void set(decimal v);
    };

    template<typename ContainerT, typename PredicateT>
    void erase_if(ContainerT& items, const PredicateT& predicate) {
        for (auto it = items.begin(); it != items.end();) {
            if (predicate(*it)) it = items.erase(it);
            else ++it;
        }
    };

    enum c2d_brush_t {
        b_static,
        b_sleep,
        b_normal,
        b_coll,
        b_F,
        b_V,
        b_D,
        b_center,
        b_bounds,
        b_drag_pt,
        b_drag_line,
        b_coll_pt,
        b_coll_line,
        b__end,
    };

    struct BrushBag {
        std::vector<CComPtr<ID2D1SolidColorBrush>> brushes;
        std::vector<CColor> colors;
    };
}


#endif //CLIB2D_C2D_H
