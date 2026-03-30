from __future__ import annotations

import csv
import json
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "thermal_input.json"
REPORT_PATH = ROOT / "thermal_report.md"
CSV_PATH = ROOT / "thermal_budget.csv"
SVG_PATH = ROOT / "thermal_led_tj_vs_rsa.svg"


def load_config() -> dict:
    return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def calc_core(cfg: dict) -> dict:
    a = cfg["assumptions"]

    current = float(a["full_power_current_a"])
    led_vf = float(a["led_forward_voltage_v"])
    rsense = float(a["sense_resistor_ohm"])
    q5_drain_to_gnd = float(a["q5_drain_to_gnd_target_v"])
    eta_led_optical = float(a["led_optical_efficiency"])
    eta_boost = float(a["boost_efficiency"])
    aux_power = float(a["aux_power_w"])

    led_power_elec = current * led_vf
    vsense = current * rsense
    q5_true_vds = max(0.0, q5_drain_to_gnd - vsense)
    q5_loss = q5_true_vds * current
    led_heat = led_power_elec * (1.0 - eta_led_optical)
    boost_out_power = led_power_elec + q5_loss
    boost_loss = boost_out_power / eta_boost - boost_out_power
    heatsink_load = led_heat + q5_loss
    total_internal_heat = heatsink_load + boost_loss + aux_power

    return {
        "current_a": current,
        "led_vf_v": led_vf,
        "led_power_elec_w": led_power_elec,
        "vsense_v": vsense,
        "q5_true_vds_v": q5_true_vds,
        "q5_loss_w": q5_loss,
        "led_heat_w": led_heat,
        "boost_loss_w": boost_loss,
        "heatsink_load_w": heatsink_load,
        "total_internal_heat_w": total_internal_heat,
    }


def calc_temps(cfg: dict, ambient_c: float, r_sa: float, core: dict) -> dict:
    a = cfg["assumptions"]
    led_r_stack = float(a["led_r_jc_c_per_w"]) + float(a["led_r_cs_c_per_w"])
    q5_r_stack = float(a["q5_r_jc_c_per_w"]) + float(a["q5_r_cs_c_per_w"])

    sink_temp = ambient_c + core["heatsink_load_w"] * r_sa
    led_tj = sink_temp + core["led_heat_w"] * led_r_stack
    q5_tj = sink_temp + core["q5_loss_w"] * q5_r_stack

    return {
        "ambient_c": ambient_c,
        "r_sa_c_per_w": r_sa,
        "sink_temp_c": sink_temp,
        "led_tj_c": led_tj,
        "q5_tj_c": q5_tj,
    }


def required_r_sa(cfg: dict, ambient_c: float, core: dict) -> float:
    a = cfg["assumptions"]
    led_r_stack = float(a["led_r_jc_c_per_w"]) + float(a["led_r_cs_c_per_w"])
    led_tj_target = float(a["led_tj_target_c"])

    numerator = led_tj_target - ambient_c - core["led_heat_w"] * led_r_stack
    denominator = core["heatsink_load_w"]
    return numerator / denominator


def format_table(rows: list[list[str]]) -> str:
    header = "| " + " | ".join(rows[0]) + " |\n"
    sep = "| " + " | ".join(["---"] * len(rows[0])) + " |\n"
    body = "".join("| " + " | ".join(row) + " |\n" for row in rows[1:])
    return header + sep + body


def write_csv(core: dict) -> None:
    rows = [
        ["component", "heat_w"],
        ["COB_LED_heat", f"{core['led_heat_w']:.2f}"],
        ["Q5_linear_loss", f"{core['q5_loss_w']:.2f}"],
        ["Boost_loss", f"{core['boost_loss_w']:.2f}"],
        ["Aux_power", "1.50"],
        ["Total_internal_heat", f"{core['total_internal_heat_w']:.2f}"],
    ]
    with CSV_PATH.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(rows)


def write_svg(cfg: dict, core: dict) -> None:
    ambients = cfg["assumptions"]["ambient_temp_list_c"]
    rsas = cfg["assumptions"]["heatsink_r_sa_candidates_c_per_w"]

    width = 1200
    height = 720
    left = 110
    right = 70
    top = 90
    bottom = 90
    plot_w = width - left - right
    plot_h = height - top - bottom

    y_min = 40
    y_max = 140

    def x_map(rsa: float) -> float:
        x0 = min(rsas)
        x1 = max(rsas)
        return left + (rsa - x0) / (x1 - x0) * plot_w

    def y_map(temp: float) -> float:
        return top + (y_max - temp) / (y_max - y_min) * plot_h

    colors = {25: "#2563eb", 35: "#f59e0b", 40: "#ef4444"}
    lines = []

    for ambient in ambients:
        points = []
        for rsa in rsas:
            t = calc_temps(cfg, float(ambient), float(rsa), core)
            points.append(f"{x_map(float(rsa)):.1f},{y_map(t['led_tj_c']):.1f}")
        polyline = (
            f'<polyline fill="none" stroke="{colors[int(ambient)]}" stroke-width="5" '
            f'points="{" ".join(points)}" />'
        )
        lines.append(polyline)

    y_ticks = []
    for temp in range(y_min, y_max + 1, 20):
        y = y_map(float(temp))
        y_ticks.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{width-right}" y2="{y:.1f}" stroke="#334155" stroke-width="1" />'
        )
        y_ticks.append(
            f'<text x="{left-15}" y="{y+6:.1f}" text-anchor="end" font-size="18" fill="#cbd5e1" '
            f'font-family="Segoe UI, Arial, sans-serif">{temp}</text>'
        )

    x_ticks = []
    for rsa in rsas:
        x = x_map(float(rsa))
        x_ticks.append(
            f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{height-bottom}" stroke="#334155" stroke-width="1" />'
        )
        x_ticks.append(
            f'<text x="{x:.1f}" y="{height-bottom+35}" text-anchor="middle" font-size="18" fill="#cbd5e1" '
            f'font-family="Segoe UI, Arial, sans-serif">{rsa:.2f}</text>'
        )

    led_target = float(cfg["assumptions"]["led_tj_target_c"])
    target_y = y_map(led_target)

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="{width}" height="{height}" fill="#0f172a"/>
  <text x="{width/2:.1f}" y="45" text-anchor="middle" font-size="34" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">
    LED Junction Temperature vs Heatsink Rth
  </text>
  <text x="{width/2:.1f}" y="75" text-anchor="middle" font-size="18" fill="#94a3b8" font-family="Segoe UI, Arial, sans-serif">
    Placeholder estimate generated from hot_design/thermal_input.json
  </text>
  <rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#111827" stroke="#475569" stroke-width="2"/>
  {"".join(y_ticks)}
  {"".join(x_ticks)}
  <line x1="{left}" y1="{target_y:.1f}" x2="{width-right}" y2="{target_y:.1f}" stroke="#22c55e" stroke-width="3" stroke-dasharray="12 8" />
  {"".join(lines)}
  <text x="{width-right-10}" y="{target_y-10:.1f}" text-anchor="end" font-size="18" fill="#22c55e" font-family="Segoe UI, Arial, sans-serif">
    LED target = {led_target:.0f} C
  </text>
  <text x="{width/2:.1f}" y="{height-20}" text-anchor="middle" font-size="20" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">
    Heatsink-to-ambient thermal resistance (C/W)
  </text>
  <g transform="translate(20, {height/2:.1f}) rotate(-90)">
    <text text-anchor="middle" font-size="20" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">
      LED junction temperature (C)
    </text>
  </g>
  <rect x="820" y="110" width="300" height="120" rx="14" fill="#0b1220" stroke="#475569" stroke-width="2"/>
  <line x1="850" y1="145" x2="910" y2="145" stroke="#2563eb" stroke-width="5"/>
  <text x="930" y="151" font-size="18" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">Ambient 25 C</text>
  <line x1="850" y1="180" x2="910" y2="180" stroke="#f59e0b" stroke-width="5"/>
  <text x="930" y="186" font-size="18" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">Ambient 35 C</text>
  <line x1="850" y1="215" x2="910" y2="215" stroke="#ef4444" stroke-width="5"/>
  <text x="930" y="221" font-size="18" fill="#e2e8f0" font-family="Segoe UI, Arial, sans-serif">Ambient 40 C</text>
</svg>
"""
    SVG_PATH.write_text(svg, encoding="utf-8")


def write_report(cfg: dict, core: dict) -> None:
    a = cfg["assumptions"]
    generated = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    summary_rows = [
        ["Item", "Value", "Comment"],
        ["Full-power current", f"{core['current_a']:.2f} A", "Placeholder, TODO verify by measurement"],
        ["COB forward voltage", f"{core['led_vf_v']:.2f} V", "Placeholder from local docs"],
        ["LED electrical power", f"{core['led_power_elec_w']:.2f} W", "Vf x I"],
        ["Sense voltage", f"{core['vsense_v']:.2f} V", "I x 0.1 ohm"],
        ["Q5 true Vds", f"{core['q5_true_vds_v']:.2f} V", "Drain-to-GND target minus source sense"],
        ["Q5 loss", f"{core['q5_loss_w']:.2f} W", "I x Vds_true"],
        ["LED heat", f"{core['led_heat_w']:.2f} W", "Assume optical efficiency = 25%"],
        ["Boost loss", f"{core['boost_loss_w']:.2f} W", "Assume boost efficiency = 92%"],
        ["Main heatsink load", f"{core['heatsink_load_w']:.2f} W", "LED heat + Q5 loss"],
        ["Total internal heat", f"{core['total_internal_heat_w']:.2f} W", "Heatsink load + boost loss + aux power"],
    ]

    required_rows = [["Ambient", "Required Rth_sa", "Comment"]]
    for ambient in a["ambient_temp_list_c"]:
        req = required_r_sa(cfg, float(ambient), core)
        required_rows.append(
            [f"{ambient} C", f"{req:.2f} C/W", "To keep LED junction <= 100 C"]
        )

    scenario_rows = [["Ambient", "Rth_sa", "Sink Temp", "LED Tj", "Q5 Tj", "LED Status"]]
    for ambient in a["ambient_temp_list_c"]:
        for rsa in a["heatsink_r_sa_candidates_c_per_w"]:
            t = calc_temps(cfg, float(ambient), float(rsa), core)
            status = "PASS" if t["led_tj_c"] <= float(a["led_tj_target_c"]) else "FAIL"
            scenario_rows.append(
                [
                    f"{ambient} C",
                    f"{rsa:.2f} C/W",
                    f"{t['sink_temp_c']:.1f} C",
                    f"{t['led_tj_c']:.1f} C",
                    f"{t['q5_tj_c']:.1f} C",
                    status,
                ]
            )

    report = f"""# Thermal Report

Generated: `{generated}`

## 1. Purpose

This folder contains a **placeholder thermal estimate** for the current `Photography Fill Light` hardware concept.

It is not a final thermal sign-off. It is only used to:

- estimate the main heatsink thermal load,
- estimate LED/Q5 temperature risk,
- give a first-pass target for heatsink-to-ambient thermal resistance,
- mark which values must be replaced by real measurements.

## 2. Placeholder Inputs That Must Be Replaced

{chr(10).join(f"- {note}" for note in cfg["todo_notes"])}

## 3. Summary

{format_table(summary_rows)}

## 4. Required Heatsink Target

{format_table(required_rows)}

**Current design recommendation:** target the main heatsink at **<= 0.35 C/W with forced airflow** if the project remains in the `60W-80W` power class.

## 5. Scenario Sweep

{format_table(scenario_rows)}

## 6. Core Equations

- `P_led_elec = V_led x I_led`
- `V_sense = I_led x R_sense`
- `Vds_true = V_drain_to_gnd_target - V_sense`
- `P_q5 = Vds_true x I_led`
- `P_led_heat = P_led_elec x (1 - optical_efficiency)`
- `P_boost_loss = P_boost_out / eta_boost - P_boost_out`
- `T_sink = T_ambient + P_heatsink x Rth_sa`
- `Tj_led = T_sink + P_led_heat x (Rth_jc_led + Rth_cs_led)`
- `Tj_q5 = T_sink + P_q5 x (Rth_jc_q5 + Rth_cs_q5)`

## 7. Engineering Notes

- The current firmware strategy tries to keep `Q5` close to low headroom at high power, so **Q5 should not be the main heatsink load** in normal operation.
- The LED heat dominates the heatsink design.
- Boost loss is still important, but it mainly affects PCB hot spots and airflow planning rather than the COB heatsink size directly.
- If measured `Q5 drain-to-GND` is higher than this placeholder, update the input file immediately. Q5 loss will rise fast.

## 8. Output Files

- `thermal_budget.csv`: component heat budget
- `thermal_led_tj_vs_rsa.svg`: LED junction temperature trend versus heatsink thermal resistance
"""
    REPORT_PATH.write_text(report, encoding="utf-8")


def main() -> None:
    cfg = load_config()
    core = calc_core(cfg)
    write_csv(core)
    write_svg(cfg, core)
    write_report(cfg, core)
    print("Thermal estimate generated:")
    print(f"  {REPORT_PATH.name}")
    print(f"  {CSV_PATH.name}")
    print(f"  {SVG_PATH.name}")


if __name__ == "__main__":
    main()
