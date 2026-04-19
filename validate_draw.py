#!/usr/bin/env python3
from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from PIL import Image


SCREEN_W = 1280
SCREEN_H = 720
DRAW_SIZE = 256
DRAW_LEFT = (SCREEN_W - DRAW_SIZE) / 2
DRAW_TOP = (SCREEN_H - DRAW_SIZE) / 2
COLOR_BOX_W = 200
COLOR_BOX_H = 100
HUE_STEPS = 200
PEN_SIZES = [0, 1, 2, 3, 4, 5]
ERASER_SIZES = [4, 8]
TOOL_ORDER = ["bucket", "pen", "eraser"]
TOOL_INDEX = {"bucket": 0, "pen": 1, "eraser": 2}
ERASER_MENU_ITEMS = [
    {"type": "brush", "label": "Small Eraser", "brushIndex": 0},
    {"type": "brush", "label": "Medium Eraser", "brushIndex": 1},
    {"type": "clear", "label": "Erase Canvas"},
]
MENU_TARGET_CACHE: dict[int, dict] = {}


def encode_color(r: int, g: int, b: int) -> int:
    return (r << 16) | (g << 8) | b


def decode_color(value: int) -> tuple[int, int, int] | None:
    if value < 0:
        return None
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


def rgb_to_hex(value: int) -> str:
    if value < 0:
        return "transparent"
    return f"#{value:06X}"


def clamp(value, minimum, maximum):
    return min(maximum, max(minimum, value))


def distance(ax: float, ay: float, bx: float, by: float) -> float:
    return math.hypot(bx - ax, by - ay)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def js_round(value: float) -> int:
    return math.floor(value + 0.5)


def rgb_to_hsv(r: int, g: int, b: int) -> tuple[float, float, float]:
    rn = r / 255
    gn = g / 255
    bn = b / 255
    maximum = max(rn, gn, bn)
    minimum = min(rn, gn, bn)
    delta = maximum - minimum
    h = 0.0
    if delta != 0:
        if maximum == rn:
            h = ((gn - bn) / delta) % 6
        elif maximum == gn:
            h = (bn - rn) / delta + 2
        else:
            h = (rn - gn) / delta + 4
        h *= 60
        if h < 0:
            h += 360
    s = 0.0 if maximum == 0 else delta / maximum
    v = maximum
    return (h, s, v)


def hsv_to_rgb(h: float, s: float, v: float) -> tuple[int, int, int]:
    hue = ((h % 360) + 360) % 360
    c = v * s
    x = c * (1 - abs(((hue / 60) % 2) - 1))
    m = v - c
    rn = gn = bn = 0.0
    if hue < 60:
        rn, gn = c, x
    elif hue < 120:
        rn, gn = x, c
    elif hue < 180:
        gn, bn = c, x
    elif hue < 240:
        gn, bn = x, c
    elif hue < 300:
        rn, bn = x, c
    else:
        rn, bn = c, x
    return (
        js_round((rn + m) * 255),
        js_round((gn + m) * 255),
        js_round((bn + m) * 255),
    )


def color_from_menu_state(hue_step: int, cursor_x: int, cursor_y: int) -> int:
    hue = (hue_step / HUE_STEPS) * 360
    saturation = clamp(cursor_x / (COLOR_BOX_W - 1), 0, 1)
    value = clamp(1 - cursor_y / (COLOR_BOX_H - 1), 0, 1)
    r, g, b = hsv_to_rgb(hue, saturation, value)
    return encode_color(r, g, b)


def color_to_menu_target(encoded: int) -> dict[str, int]:
    color = decode_color(encoded)
    if color is None:
        return {"hueStep": 0, "x": 0, "y": COLOR_BOX_H - 1}
    h, s, v = rgb_to_hsv(*color)
    hue_step = ((round((h / 360) * HUE_STEPS) % HUE_STEPS) + HUE_STEPS) % HUE_STEPS
    return {
        "hueStep": hue_step,
        "x": clamp(js_round(s * (COLOR_BOX_W - 1)), 0, COLOR_BOX_W - 1),
        "y": clamp(js_round((1 - v) * (COLOR_BOX_H - 1)), 0, COLOR_BOX_H - 1),
    }


def color_distance_sq(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return sum((left - right) ** 2 for left, right in zip(a, b))


def find_nearest_menu_target(encoded: int) -> dict:
    if encoded in MENU_TARGET_CACHE:
        return MENU_TARGET_CACHE[encoded]
    color = decode_color(encoded)
    if color is None:
        fallback = {"hueStep": 0, "x": 0, "y": COLOR_BOX_H - 1, "actualColor": -1}
        MENU_TARGET_CACHE[encoded] = fallback
        return fallback

    approximate = color_to_menu_target(encoded)
    best_actual = color_from_menu_state(approximate["hueStep"], approximate["x"], approximate["y"])
    best = {
        "hueStep": approximate["hueStep"],
        "x": approximate["x"],
        "y": approximate["y"],
        "actualColor": best_actual,
    }
    best_distance = color_distance_sq(decode_color(best_actual), color)

    for hue_offset in range(-4, 5):
        hue_step = (approximate["hueStep"] + hue_offset + HUE_STEPS) % HUE_STEPS
        for y in range(COLOR_BOX_H):
            for x in range(COLOR_BOX_W):
                actual = color_from_menu_state(hue_step, x, y)
                distance_sq = color_distance_sq(decode_color(actual), color)
                if distance_sq < best_distance:
                    best_distance = distance_sq
                    best = {"hueStep": hue_step, "x": x, "y": y, "actualColor": actual}
                    if distance_sq == 0:
                        MENU_TARGET_CACHE[encoded] = best
                        return best

    MENU_TARGET_CACHE[encoded] = best
    return best


def create_image_array(fill: int = -1) -> list[int]:
    return [fill] * (DRAW_SIZE * DRAW_SIZE)


def screen_to_canvas_coord(x: float, y: float) -> tuple[float, float]:
    return (x - DRAW_LEFT, y - DRAW_TOP)


def canvas_to_screen_coord(x: float, y: float) -> tuple[float, float]:
    return (DRAW_LEFT + x + 0.5, DRAW_TOP + y + 0.5)


def trace_canvas_to_screen_coord(x: float, y: float) -> tuple[float, float]:
    return (DRAW_LEFT + x, DRAW_TOP + y)


def is_inside_canvas(x: int, y: int) -> bool:
    return 0 <= x < DRAW_SIZE and 0 <= y < DRAW_SIZE


def neighbors4(index: int) -> list[int]:
    x = index % DRAW_SIZE
    y = index // DRAW_SIZE
    out = []
    if x > 0:
      out.append(index - 1)
    if x < DRAW_SIZE - 1:
      out.append(index + 1)
    if y > 0:
      out.append(index - DRAW_SIZE)
    if y < DRAW_SIZE - 1:
      out.append(index + DRAW_SIZE)
    return out


@dataclass
class ProcessedImage:
    target: list[int]
    palette: list[dict]


@dataclass
class Component:
    pixels: list[int]
    component_mask: list[int]
    boundary_mask: list[int]
    boundary_pixels: list[int]
    walks: list[list[dict]]
    area: int


def build_scaled_image(image_path: Path) -> Image.Image:
    image = Image.open(image_path).convert("RGBA")
    scale = min(1, DRAW_SIZE / image.width, DRAW_SIZE / image.height)
    width = max(1, js_round(image.width * scale))
    height = max(1, js_round(image.height * scale))
    offset_x = (DRAW_SIZE - width) // 2
    offset_y = (DRAW_SIZE - height) // 2
    canvas = Image.new("RGBA", (DRAW_SIZE, DRAW_SIZE), (0, 0, 0, 0))
    resized = image.resize((width, height), Image.Resampling.BILINEAR)
    canvas.alpha_composite(resized, (offset_x, offset_y))
    return canvas


def kmeans_palette(unique_colors: list[dict], k: int) -> list[dict]:
    centers = []
    first = max(unique_colors, key=lambda entry: entry["count"])
    centers.append({"r": first["r"], "g": first["g"], "b": first["b"]})
    while len(centers) < k:
        best_entry = unique_colors[0]
        best_score = -1
        for entry in unique_colors:
            nearest = min(
                (center["r"] - entry["r"]) ** 2
                + (center["g"] - entry["g"]) ** 2
                + (center["b"] - entry["b"]) ** 2
                for center in centers
            )
            weighted = nearest * entry["count"]
            if weighted > best_score:
                best_score = weighted
                best_entry = entry
        centers.append({"r": best_entry["r"], "g": best_entry["g"], "b": best_entry["b"]})

    for _ in range(10):
        sums = [{"r": 0, "g": 0, "b": 0, "weight": 0} for _ in centers]
        for entry in unique_colors:
            best_index = 0
            best_distance = float("inf")
            for i, center in enumerate(centers):
                dist = (
                    (center["r"] - entry["r"]) ** 2
                    + (center["g"] - entry["g"]) ** 2
                    + (center["b"] - entry["b"]) ** 2
                )
                if dist < best_distance:
                    best_distance = dist
                    best_index = i
            sums[best_index]["r"] += entry["r"] * entry["count"]
            sums[best_index]["g"] += entry["g"] * entry["count"]
            sums[best_index]["b"] += entry["b"] * entry["count"]
            sums[best_index]["weight"] += entry["count"]
        for i, summary in enumerate(sums):
            if summary["weight"]:
                centers[i] = {
                    "r": js_round(summary["r"] / summary["weight"]),
                    "g": js_round(summary["g"] / summary["weight"]),
                    "b": js_round(summary["b"] / summary["weight"]),
                }
    return centers


def quantize_image(scaled: Image.Image, color_limit: int) -> ProcessedImage:
    pixels = list(scaled.getdata())
    counts = Counter()
    original_colors: list[int] = []
    for r, g, b, a in pixels:
        if a < 16:
            original_colors.append(-1)
            continue
        key = encode_color(r, g, b)
        original_colors.append(key)
        counts[key] += 1

    unique_colors = [
        {
            "color": color,
            "count": count,
            "r": (color >> 16) & 255,
            "g": (color >> 8) & 255,
            "b": color & 255,
        }
        for color, count in counts.items()
    ]
    if not unique_colors:
        return ProcessedImage(create_image_array(-1), [])

    if len(unique_colors) <= color_limit:
        palette = [{"r": entry["r"], "g": entry["g"], "b": entry["b"]} for entry in unique_colors]
    else:
        palette = kmeans_palette(unique_colors, color_limit)

    snapped_palette_map: dict[int, dict] = {}
    for candidate in palette:
        requested = encode_color(candidate["r"], candidate["g"], candidate["b"])
        nearest = find_nearest_menu_target(requested)
        decoded = decode_color(nearest["actualColor"])
        snapped_palette_map[nearest["actualColor"]] = {
            "r": decoded[0],
            "g": decoded[1],
            "b": decoded[2],
            "actualColor": nearest["actualColor"],
        }
    snapped_palette = list(snapped_palette_map.values())

    palette_counts = Counter()
    target = create_image_array(-1)
    for i, color in enumerate(original_colors):
        if color < 0:
            continue
        r = (color >> 16) & 255
        g = (color >> 8) & 255
        b = color & 255
        best = min(
            snapped_palette,
            key=lambda candidate: (
                (candidate["r"] - r) ** 2
                + (candidate["g"] - g) ** 2
                + (candidate["b"] - b) ** 2
            ),
        )
        encoded = best["actualColor"]
        target[i] = encoded
        palette_counts[encoded] += 1

    palette_summary = [
        {"color": color, "count": count}
        for color, count in palette_counts.most_common()
    ]
    return ProcessedImage(target, palette_summary)


def build_boundary_mask(component_mask: list[int], pixels: list[int]) -> tuple[list[int], list[int]]:
    boundary_mask = [0] * len(component_mask)
    boundary_pixels = []
    for index in pixels:
        x = index % DRAW_SIZE
        y = index // DRAW_SIZE
        is_boundary = x == 0 or y == 0 or x == DRAW_SIZE - 1 or y == DRAW_SIZE - 1
        if not is_boundary:
            for neighbor in neighbors4(index):
                if not component_mask[neighbor]:
                    is_boundary = True
                    break
        if is_boundary:
            boundary_mask[index] = 1
            boundary_pixels.append(index)
    return boundary_mask, boundary_pixels


def point_key(x: int, y: int) -> str:
    return f"{x},{y}"


def same_point(a: dict, b: dict) -> bool:
    return a["x"] == b["x"] and a["y"] == b["y"]


def direction_code(dx: int, dy: int) -> int:
    if dx == 1 and dy == 0:
        return 0
    if dx == 0 and dy == 1:
        return 1
    if dx == -1 and dy == 0:
        return 2
    return 3


def choose_next_contour_edge(current_edge: dict, candidates: list[int], edges: list[dict]) -> int:
    if len(candidates) == 1:
        return candidates[0]
    current_direction = direction_code(
        current_edge["endX"] - current_edge["startX"],
        current_edge["endY"] - current_edge["startY"],
    )
    turn_preference = [1, 0, 3, 2]
    best_index = candidates[0]
    best_score = float("inf")
    for candidate_index in candidates:
        candidate = edges[candidate_index]
        candidate_direction = direction_code(
            candidate["endX"] - candidate["startX"],
            candidate["endY"] - candidate["startY"],
        )
        delta = (candidate_direction - current_direction + 4) % 4
        score = turn_preference.index(delta)
        if score < best_score:
            best_score = score
            best_index = candidate_index
    return best_index


def build_contour_loops(component_mask: list[int], pixels: list[int]) -> list[list[dict]]:
    edges: list[dict] = []
    outgoing: dict[str, list[int]] = {}

    def add_edge(start_x: int, start_y: int, end_x: int, end_y: int, pixel_x: int, pixel_y: int):
        edge = {
            "startX": start_x,
            "startY": start_y,
            "endX": end_x,
            "endY": end_y,
            "pixelX": pixel_x,
            "pixelY": pixel_y,
        }
        index = len(edges)
        edges.append(edge)
        key = point_key(start_x, start_y)
        outgoing.setdefault(key, []).append(index)

    for index in pixels:
        x = index % DRAW_SIZE
        y = index // DRAW_SIZE
        if y == 0 or not component_mask[index - DRAW_SIZE]:
            add_edge(x, y, x + 1, y, x, y)
        if x == DRAW_SIZE - 1 or not component_mask[index + 1]:
            add_edge(x + 1, y, x + 1, y + 1, x, y)
        if y == DRAW_SIZE - 1 or not component_mask[index + DRAW_SIZE]:
            add_edge(x + 1, y + 1, x, y + 1, x, y)
        if x == 0 or not component_mask[index - 1]:
            add_edge(x, y + 1, x, y, x, y)

    used = [0] * len(edges)
    loops: list[list[dict]] = []
    for edge_index in range(len(edges)):
        if used[edge_index]:
            continue
        start_edge = edges[edge_index]
        start_key = point_key(start_edge["startX"], start_edge["startY"])
        loop_edges = []
        current_index = edge_index
        while not used[current_index]:
            edge = edges[current_index]
            used[current_index] = 1
            loop_edges.append(edge)
            next_key = point_key(edge["endX"], edge["endY"])
            if next_key == start_key:
                break
            candidates = [candidate for candidate in outgoing.get(next_key, []) if not used[candidate]]
            if not candidates:
                break
            current_index = choose_next_contour_edge(edge, candidates, edges)

        path = []
        for edge in loop_edges:
            point = {"x": edge["pixelX"], "y": edge["pixelY"]}
            if not path or not same_point(path[-1], point):
                path.append(point)
        if len(path) > 1 and not same_point(path[0], path[-1]):
            path.append({"x": path[0]["x"], "y": path[0]["y"]})
        if path:
            loops.append(path)
    return loops


def build_component_data(target: list[int], color: int) -> list[Component]:
    visited = [0] * len(target)
    components = []
    for i in range(len(target)):
        if visited[i] or target[i] != color:
            continue
        queue = [i]
        visited[i] = 1
        pixels = []
        while queue:
            index = queue.pop()
            pixels.append(index)
            for neighbor in neighbors4(index):
                if not visited[neighbor] and target[neighbor] == color:
                    visited[neighbor] = 1
                    queue.append(neighbor)
        component_mask = [0] * len(target)
        for index in pixels:
            component_mask[index] = 1
        boundary_mask, boundary_pixels = build_boundary_mask(component_mask, pixels)
        walks = build_contour_loops(component_mask, pixels)
        components.append(Component(
            pixels=pixels,
            component_mask=component_mask,
            boundary_mask=boundary_mask,
            boundary_pixels=boundary_pixels,
            walks=walks,
            area=len(pixels),
        ))
    components.sort(key=lambda component: component.area, reverse=True)
    return components


def stamp_on_pixels(pixels: list[int], cx: int, cy: int, radius: int, color: int):
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            if not is_inside_canvas(x, y):
                continue
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= radius * radius:
                pixels[y * DRAW_SIZE + x] = color


def draw_stroke_on_pixels(pixels: list[int], previous_pos: dict, current_pos: dict, color: int, radius: int):
    length = max(1, math.ceil(distance(previous_pos["x"], previous_pos["y"], current_pos["x"], current_pos["y"]) * 2))
    for i in range(length + 1):
        t = i / length
        x = js_round(lerp(previous_pos["x"], current_pos["x"], t))
        y = js_round(lerp(previous_pos["y"], current_pos["y"], t))
        stamp_on_pixels(pixels, x, y, radius, color)


def simulate_loop_on_planner(pixels: list[int], walk: list[dict], color: int):
    if not walk:
        return
    if len(walk) == 1:
        stamp_on_pixels(pixels, walk[0]["x"], walk[0]["y"], PEN_SIZES[0], color)
        return
    for i in range(1, len(walk)):
        draw_stroke_on_pixels(pixels, walk[i - 1], walk[i], color, PEN_SIZES[0])


def bucket_fill_on_pixels(pixels: list[int], x: int, y: int, replacement_color: int):
    if not is_inside_canvas(x, y):
        return
    start_index = y * DRAW_SIZE + x
    target_color = pixels[start_index]
    if target_color == replacement_color:
        return
    queue = [start_index]
    visited = [0] * len(pixels)
    visited[start_index] = 1
    while queue:
        index = queue.pop()
        if pixels[index] != target_color:
            continue
        pixels[index] = replacement_color
        for neighbor in neighbors4(index):
            if not visited[neighbor] and pixels[neighbor] == target_color:
                visited[neighbor] = 1
                queue.append(neighbor)


def build_planner_fill_seeds(component: Component, planner_pixels: list[int], target_color: int) -> list[dict]:
    visited = [0] * len(component.component_mask)
    seeds = []
    for i in range(len(component.component_mask)):
        if not component.component_mask[i] or component.boundary_mask[i] or visited[i]:
            continue
        base_color = planner_pixels[i]
        queue = [i]
        visited[i] = 1
        best = i
        best_neighbors = -1
        while queue:
            index = queue.pop()
            same_neighbors = 0
            for neighbor in neighbors4(index):
                if (
                    component.component_mask[neighbor]
                    and not component.boundary_mask[neighbor]
                    and planner_pixels[neighbor] == base_color
                ):
                    same_neighbors += 1
            if same_neighbors > best_neighbors:
                best_neighbors = same_neighbors
                best = index
            for neighbor in neighbors4(index):
                if (
                    not visited[neighbor]
                    and component.component_mask[neighbor]
                    and not component.boundary_mask[neighbor]
                    and planner_pixels[neighbor] == base_color
                ):
                    visited[neighbor] = 1
                    queue.append(neighbor)
        if base_color != target_color:
            seeds.append({"x": best % DRAW_SIZE, "y": best // DRAW_SIZE, "baseColor": base_color})
    return seeds


def evaluate_planner_fill(component: Component, planner_pixels: list[int], target_color: int) -> dict:
    before = planner_pixels[:]
    after = planner_pixels[:]
    seeds = build_planner_fill_seeds(component, after, target_color)
    for seed in seeds:
        bucket_fill_on_pixels(after, seed["x"], seed["y"], target_color)

    leak = any((not component.component_mask[i]) and after[i] != before[i] for i in range(len(after)))
    complete = all(after[index] == target_color for index in component.pixels)
    return {
        "safe": complete and not leak,
        "complete": complete,
        "leak": leak,
        "seeds": seeds,
        "after": after,
    }


def build_pen_fill_runs(component: Component, planner_pixels: list[int], target_color: int) -> list[dict]:
    runs = []
    for y in range(DRAW_SIZE):
        x = 0
        while x < DRAW_SIZE:
            index = y * DRAW_SIZE + x
            if not component.component_mask[index] or planner_pixels[index] == target_color:
                x += 1
                continue
            start_x = x
            while x + 1 < DRAW_SIZE:
                next_index = y * DRAW_SIZE + (x + 1)
                if not component.component_mask[next_index] or planner_pixels[next_index] == target_color:
                    break
                x += 1
            runs.append({"y": y, "startX": start_x, "endX": x})
            x += 1
    return runs


def build_run_points(run: dict, reversed_run: bool = False) -> list[dict]:
    points = []
    if reversed_run:
        for x in range(run["endX"], run["startX"] - 1, -1):
            points.append({"x": x, "y": run["y"]})
    else:
        for x in range(run["startX"], run["endX"] + 1):
            points.append({"x": x, "y": run["y"]})
    return points


def nearest_walk_order(items: list[dict], current_screen_pos: dict, point_accessor: Callable) -> list[dict]:
    remaining = items[:]
    ordered = []
    cursor = dict(current_screen_pos)
    while remaining:
        best_index = 0
        best_distance = float("inf")
        for i, item in enumerate(remaining):
            point = point_accessor(item)
            sx, sy = canvas_to_screen_coord(point["x"], point["y"])
            dist = distance(cursor["x"], cursor["y"], sx, sy)
            if dist < best_distance:
                best_distance = dist
                best_index = i
        chosen = remaining.pop(best_index)
        ordered.append(chosen)
        end_point = point_accessor(chosen, True)
        sx, sy = canvas_to_screen_coord(end_point["x"], end_point["y"])
        cursor = {"x": sx, "y": sy}
    return ordered


def order_pen_runs(runs: list[dict], current_screen_pos: dict) -> list[dict]:
    remaining = runs[:]
    ordered = []
    cursor = dict(current_screen_pos)
    while remaining:
        best_index = 0
        best_reversed = False
        best_distance = float("inf")
        for i, run in enumerate(remaining):
            left_x, left_y = canvas_to_screen_coord(run["startX"], run["y"])
            right_x, right_y = canvas_to_screen_coord(run["endX"], run["y"])
            left_distance = distance(cursor["x"], cursor["y"], left_x, left_y)
            right_distance = distance(cursor["x"], cursor["y"], right_x, right_y)
            if left_distance < best_distance:
                best_distance = left_distance
                best_index = i
                best_reversed = False
            if right_distance < best_distance:
                best_distance = right_distance
                best_index = i
                best_reversed = True
        run = remaining.pop(best_index)
        ordered.append({"run": run, "reversed": best_reversed})
        end_x, end_y = canvas_to_screen_coord(run["startX"] if best_reversed else run["endX"], run["y"])
        cursor = {"x": end_x, "y": end_y}
    return ordered


def push_wait(commands: list[dict], frames: int, label: str = "Wait"):
    if frames > 0:
        commands.append({"type": "wait", "frames": frames, "label": label})


def push_tap(commands: list[dict], button: str, label: str | None = None, hold_frames: int = 1, release_frames: int = 1):
    commands.append({
        "type": "tap",
        "button": button,
        "label": label or f"Tap {button}",
        "holdFrames": hold_frames,
        "releaseFrames": release_frames,
    })


def push_move_cursor(commands: list[dict], x: float, y: float, label: str):
    commands.append({"type": "moveCursor", "x": x, "y": y, "label": label})


def push_move_color_cursor(commands: list[dict], x: int, y: int, label: str):
    commands.append({"type": "moveColorCursor", "x": x, "y": y, "label": label})


def push_open_pen_settings(commands: list[dict]):
    push_tap(commands, "X", "Open tool menu")
    push_wait(commands, 1, "Wait 2 frames total")
    push_tap(commands, "X", "Open pen settings")
    for i in range(5):
        push_tap(commands, "LEFT", f"Pen brush left {i + 1}")
    push_tap(commands, "A", "Confirm smallest pen")


def push_open_eraser_settings(commands: list[dict]):
    push_tap(commands, "X", "Open tool menu")
    push_wait(commands, 1, "Wait 2 frames total")
    push_tap(commands, "RIGHT", "Select eraser")
    push_tap(commands, "X", "Open eraser settings")
    push_tap(commands, "DOWN", "Move to erase canvas 1")
    push_tap(commands, "DOWN", "Move to erase canvas 2")
    push_tap(commands, "A", "Erase canvas")


def push_switch_tool(commands: list[dict], current_tool: str, target_tool: str) -> str:
    if current_tool == target_tool:
        return current_tool
    push_tap(commands, "X", "Open tool menu")
    push_wait(commands, 1, "Wait 2 frames total")
    current_index = TOOL_INDEX[current_tool]
    target_index = TOOL_INDEX[target_tool]
    direction = "LEFT" if target_index < current_index else "RIGHT"
    steps = abs(target_index - current_index)
    for i in range(steps):
        push_tap(commands, direction, f"Move to {target_tool} {i + 1}/{steps}")
    push_tap(commands, "A", f"Confirm {target_tool}")
    return target_tool


def push_select_color(commands: list[dict], current_hue_step: int, target_color: int) -> int:
    target = find_nearest_menu_target(target_color)
    push_tap(commands, "Y", "Color menu step 1")
    push_wait(commands, 1, "Wait 2 frames total")
    push_tap(commands, "Y", "Color menu step 2")
    push_tap(commands, "R", "Color menu confirm step")
    push_wait(commands, 1, "Wait 2 frames total")

    hue = current_hue_step
    forward = (target["hueStep"] - hue + HUE_STEPS) % HUE_STEPS
    backward = (hue - target["hueStep"] + HUE_STEPS) % HUE_STEPS
    if forward <= backward:
        for i in range(forward):
            push_tap(commands, "ZR", f"Hue + {i + 1}")
        hue = target["hueStep"]
    else:
        for i in range(backward):
            push_tap(commands, "ZL", f"Hue - {i + 1}")
        hue = target["hueStep"]

    push_move_color_cursor(commands, target["x"], target["y"], "Move color cursor")
    push_tap(commands, "A", f"Confirm {rgb_to_hex(target_color)}")
    return hue


def build_plan(processed: ProcessedImage) -> tuple[list[dict], list[int]]:
    commands = []
    planner_pixels = create_image_array(-1)
    current_tool = "pen"
    current_hue_step = 0
    cursor_screen = {"x": SCREEN_W / 2, "y": SCREEN_H / 2}

    push_open_pen_settings(commands)
    push_open_eraser_settings(commands)
    current_tool = "pen"

    colors = [entry["color"] for entry in processed.palette]
    for color in colors:
        components = build_component_data(processed.target, color)
        current_hue_step = push_select_color(commands, current_hue_step, color)
        current_tool = push_switch_tool(commands, current_tool, "pen")
        for component in components:
            loops = [
                {"walk": walk, "start": walk[0], "end": walk[-1]}
                for walk in component.walks
                if walk
            ]
            ordered_loops = nearest_walk_order(
                loops,
                cursor_screen,
                lambda item, use_end=False: item["end"] if use_end else item["start"],
            )

            current_tool = push_switch_tool(commands, current_tool, "pen")
            for loop in ordered_loops:
                start_x, start_y = trace_canvas_to_screen_coord(loop["start"]["x"], loop["start"]["y"])
                push_move_cursor(commands, start_x, start_y, f"Move to outline {rgb_to_hex(color)}")
                if len(loop["walk"]) == 1:
                    push_tap(commands, "A", f"Dot outline {rgb_to_hex(color)}")
                else:
                    commands.append({
                        "type": "tracePath",
                        "points": loop["walk"],
                        "label": f"Trace outline {rgb_to_hex(color)}",
                    })
                simulate_loop_on_planner(planner_pixels, loop["walk"], color)
                end_x, end_y = trace_canvas_to_screen_coord(loop["end"]["x"], loop["end"]["y"])
                cursor_screen = {"x": end_x, "y": end_y}

            fill_plan = evaluate_planner_fill(component, planner_pixels, color)
            if fill_plan["safe"] and fill_plan["seeds"]:
                current_tool = push_switch_tool(commands, current_tool, "bucket")
                ordered_seeds = nearest_walk_order(
                    [{"start": seed, "end": seed} for seed in fill_plan["seeds"]],
                    cursor_screen,
                    lambda item, _use_end=False: item["start"],
                )
                for item in ordered_seeds:
                    seed = item["start"]
                    sx, sy = canvas_to_screen_coord(seed["x"], seed["y"])
                    push_move_cursor(commands, sx, sy, f"Move to fill {rgb_to_hex(color)}")
                    push_tap(commands, "A", f"Bucket fill {rgb_to_hex(color)}")
                    cursor_screen = {"x": sx, "y": sy}
                planner_pixels = fill_plan["after"][:]
            elif (not fill_plan["complete"]) or fill_plan["leak"]:
                current_tool = push_switch_tool(commands, current_tool, "pen")
                runs = build_pen_fill_runs(component, planner_pixels, color)
                ordered_runs = order_pen_runs(runs, cursor_screen)
                for entry in ordered_runs:
                    points = build_run_points(entry["run"], entry["reversed"])
                    sx, sy = trace_canvas_to_screen_coord(points[0]["x"], points[0]["y"])
                    push_move_cursor(commands, sx, sy, f"Move to cleanup {rgb_to_hex(color)}")
                    if len(points) == 1:
                        push_tap(commands, "A", f"Dot cleanup {rgb_to_hex(color)}")
                    else:
                        commands.append({
                            "type": "tracePath",
                            "points": points,
                            "label": f"Pen fill cleanup {rgb_to_hex(color)}",
                        })
                    simulate_loop_on_planner(planner_pixels, points, color)
                    ex, ey = trace_canvas_to_screen_coord(points[-1]["x"], points[-1]["y"])
                    cursor_screen = {"x": ex, "y": ey}

    current_tool = push_switch_tool(commands, current_tool, "pen")
    return commands, planner_pixels


class AbstractGame:
    def __init__(self):
        self.pixels = create_image_array(-1)
        self.cursor = {"x": SCREEN_W / 2, "y": SCREEN_H / 2}
        self.mode = "canvas"
        self.active_tool = "pen"
        self.menu_tool_selection = TOOL_INDEX["pen"]
        self.pen_brush_index = 5
        self.eraser_brush_index = 0
        self.eraser_menu_index = 0
        self.tool_before_eraser_menu = "pen"
        self.color_hue_step = 0
        self.color_cursor = {"x": 0, "y": COLOR_BOX_H - 1}
        self.selected_color = encode_color(0, 0, 0)
        self.color_sequence_step = 0

    def get_brush_radius(self) -> int:
        if self.active_tool == "eraser":
            return ERASER_SIZES[self.eraser_brush_index]
        return PEN_SIZES[self.pen_brush_index]

    def stamp_at_cursor(self):
        canvas_x, canvas_y = screen_to_canvas_coord(self.cursor["x"], self.cursor["y"])
        color = -1 if self.active_tool == "eraser" else self.selected_color
        stamp_on_pixels(self.pixels, math.floor(canvas_x), math.floor(canvas_y), self.get_brush_radius(), color)

    def bucket_at_cursor(self):
        canvas_x, canvas_y = screen_to_canvas_coord(self.cursor["x"], self.cursor["y"])
        bucket_fill_on_pixels(self.pixels, math.floor(canvas_x), math.floor(canvas_y), self.selected_color)

    def erase_canvas(self):
        for i in range(len(self.pixels)):
            self.pixels[i] = -1

    def press(self, button: str):
        if self.mode == "canvas":
            if self.color_sequence_step == 0:
                if button == "Y":
                    self.color_sequence_step = 1
                    return
            elif self.color_sequence_step == 1:
                if button == "Y":
                    self.color_sequence_step = 2
                    return
                self.color_sequence_step = 0
            elif self.color_sequence_step == 2:
                if button == "R":
                    self.mode = "colorMenu"
                    self.color_sequence_step = 0
                    return
                self.color_sequence_step = 0

            if button == "X":
                self.mode = "toolMenu"
                self.menu_tool_selection = TOOL_INDEX[self.active_tool]
            elif button == "A":
                if self.active_tool == "bucket":
                    self.bucket_at_cursor()
                elif self.active_tool in ("pen", "eraser"):
                    self.stamp_at_cursor()
        elif self.mode == "toolMenu":
            if button == "LEFT":
                self.menu_tool_selection = clamp(self.menu_tool_selection - 1, 0, len(TOOL_ORDER) - 1)
            elif button == "RIGHT":
                self.menu_tool_selection = clamp(self.menu_tool_selection + 1, 0, len(TOOL_ORDER) - 1)
            elif button == "B":
                self.mode = "canvas"
            elif button == "A":
                self.active_tool = TOOL_ORDER[self.menu_tool_selection]
                self.mode = "canvas"
            elif button == "X":
                selected_tool = TOOL_ORDER[self.menu_tool_selection]
                if selected_tool == "pen":
                    self.mode = "penSettings"
                elif selected_tool == "eraser":
                    self.tool_before_eraser_menu = "pen" if self.active_tool == "eraser" else self.active_tool
                    self.eraser_menu_index = self.eraser_brush_index
                    self.mode = "eraserSettings"
        elif self.mode == "penSettings":
            if button == "LEFT":
                self.pen_brush_index = clamp(self.pen_brush_index - 1, 0, len(PEN_SIZES) - 1)
            elif button == "RIGHT":
                self.pen_brush_index = clamp(self.pen_brush_index + 1, 0, len(PEN_SIZES) - 1)
            elif button == "B":
                self.mode = "toolMenu"
            elif button == "A":
                self.active_tool = "pen"
                self.mode = "canvas"
        elif self.mode == "eraserSettings":
            if button == "UP":
                self.eraser_menu_index = clamp(self.eraser_menu_index - 1, 0, len(ERASER_MENU_ITEMS) - 1)
            elif button == "DOWN":
                self.eraser_menu_index = clamp(self.eraser_menu_index + 1, 0, len(ERASER_MENU_ITEMS) - 1)
            elif button == "B":
                self.mode = "toolMenu"
            elif button == "A":
                item = ERASER_MENU_ITEMS[self.eraser_menu_index]
                if item["type"] == "clear":
                    self.erase_canvas()
                    self.active_tool = self.tool_before_eraser_menu or "pen"
                else:
                    self.eraser_brush_index = item["brushIndex"]
                    self.active_tool = "eraser"
                self.mode = "canvas"
        elif self.mode == "colorMenu":
            if button == "ZR":
                self.color_hue_step = (self.color_hue_step + 1) % HUE_STEPS
            elif button == "ZL":
                self.color_hue_step = (self.color_hue_step - 1 + HUE_STEPS) % HUE_STEPS
            elif button == "B":
                self.mode = "canvas"
            elif button == "A":
                self.selected_color = color_from_menu_state(
                    self.color_hue_step,
                    js_round(self.color_cursor["x"]),
                    js_round(self.color_cursor["y"]),
                )
                self.mode = "canvas"

    def execute_trace(self, points: list[dict]):
        if self.mode != "canvas":
            return
        if self.active_tool not in ("pen", "eraser"):
            return
        if not points:
            return
        color = -1 if self.active_tool == "eraser" else self.selected_color
        if len(points) == 1:
            stamp_on_pixels(self.pixels, points[0]["x"], points[0]["y"], self.get_brush_radius(), color)
            self.cursor["x"], self.cursor["y"] = canvas_to_screen_coord(points[0]["x"], points[0]["y"])
            return
        for i in range(1, len(points)):
            draw_stroke_on_pixels(self.pixels, points[i - 1], points[i], color, self.get_brush_radius())
        self.cursor["x"], self.cursor["y"] = trace_canvas_to_screen_coord(points[-1]["x"], points[-1]["y"])

    def execute(self, commands: list[dict]):
        for command in commands:
            command_type = command["type"]
            if command_type == "wait":
                continue
            if command_type == "tap":
                self.press(command["button"])
                continue
            if command_type == "moveCursor":
                self.cursor["x"] = command["x"]
                self.cursor["y"] = command["y"]
                continue
            if command_type == "moveColorCursor":
                self.color_cursor["x"] = command["x"]
                self.color_cursor["y"] = command["y"]
                continue
            if command_type == "tracePath":
                self.execute_trace(command["points"])
                continue


def compare_pixels(a: list[int], b: list[int]) -> tuple[int, int]:
    matches = sum(1 for left, right in zip(a, b) if left == right)
    return matches, len(b)


def save_pixel_image(pixels: list[int], output: Path):
    image = Image.new("RGBA", (DRAW_SIZE, DRAW_SIZE), (0, 0, 0, 0))
    rgba = []
    for color in pixels:
        decoded = decode_color(color)
        if decoded is None:
            rgba.append((0, 0, 0, 0))
        else:
            rgba.append((*decoded, 255))
    image.putdata(rgba)
    image.save(output)


def save_diff_image(actual: list[int], target: list[int], output: Path):
    image = Image.new("RGBA", (DRAW_SIZE, DRAW_SIZE), (0, 0, 0, 0))
    rgba = []
    for left, right in zip(actual, target):
        if left == right:
            rgba.append((0, 0, 0, 0))
        else:
            rgba.append((255, 0, 120, 255))
    image.putdata(rgba)
    image.save(output)


def main():
    image_path = Path("/Users/thebe/Downloads/133.png")
    scaled = build_scaled_image(image_path)
    processed = quantize_image(scaled, 2)
    commands, planner_pixels = build_plan(processed)

    planner_matches, total = compare_pixels(planner_pixels, processed.target)
    print("palette:", [rgb_to_hex(entry["color"]) for entry in processed.palette])
    print("commands:", len(commands))
    print("planner match:", f"{planner_matches}/{total}", f"{planner_matches / total:.4%}")

    abstract_game = AbstractGame()
    abstract_game.execute(commands)
    actual_matches, total = compare_pixels(abstract_game.pixels, processed.target)
    print("abstract execution match:", f"{actual_matches}/{total}", f"{actual_matches / total:.4%}")
    print("final tool:", abstract_game.active_tool, "mode:", abstract_game.mode, "color:", rgb_to_hex(abstract_game.selected_color))

    output_dir = Path("/Users/thebe/Documents/emulatorcursor/.validation")
    output_dir.mkdir(exist_ok=True)
    scaled.save(output_dir / "scaled.png")
    save_pixel_image(processed.target, output_dir / "target_quant2.png")
    save_pixel_image(planner_pixels, output_dir / "planner_pixels.png")
    save_pixel_image(abstract_game.pixels, output_dir / "abstract_execution.png")
    save_diff_image(abstract_game.pixels, processed.target, output_dir / "diff.png")
    print("wrote:", output_dir)


if __name__ == "__main__":
    main()
