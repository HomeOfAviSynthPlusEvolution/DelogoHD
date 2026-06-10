from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from typing import Iterable

_LOADED_VS_PLUGINS: set[str] = set()
GOLDEN_SCHEMA = 2
PLUGIN_NAMESPACE = "delogohd"
DEFAULT_FILTER = "DelogoHD"
PATH_PARAM_KEYS = {"logofile"}
BASELINE_DIR = Path(__file__).resolve().parent


@dataclass(frozen=True)
class PlaneBytes:
    name: str
    width: int
    height: int
    bytes_per_sample: int
    stride: int
    data: bytes

    @property
    def row_bytes(self) -> int:
        return self.width * self.bytes_per_sample


def hash_planes(planes: Iterable[PlaneBytes]) -> str:
    digest = hashlib.sha256()
    for plane in planes:
        row_bytes = plane.row_bytes
        for y in range(plane.height):
            start = y * plane.stride
            digest.update(plane.data[start:start + row_bytes])
    return digest.hexdigest()


def select_cases(cases: list[dict], tier: str) -> list[dict]:
    if tier == "all":
        return list(cases)
    return [case for case in cases if case.get("tier") == tier]


def render_vs_script(plugin_path: str, source: dict, filter_name: str, params: dict) -> str:
    param_text = ", ".join(
        f"{key}={_format_vs_value(params[key])}" for key in sorted(params)
    )
    if param_text:
        param_text = ", " + param_text

    source_lines = _render_vs_source_lines(source)
    return "\n".join(
        [
            "import vapoursynth as vs",
            "core = vs.core",
            f"core.std.LoadPlugin(path={json.dumps(plugin_path)})",
            *source_lines,
            f"clip = core.{PLUGIN_NAMESPACE}.{filter_name}(src{param_text})",
            "clip.set_output()",
            "",
        ]
    )


def render_avs_script(plugin_path: str, source: dict, filter_name: str, params: dict) -> str:
    param_text = ", ".join(
        f"{key}={_format_avs_value(params[key])}" for key in sorted(params)
    )
    if param_text:
        param_text = ", " + param_text

    source_lines = _render_avs_source_lines(source)
    return "\n".join(
        [
            f"LoadPlugin({json.dumps(plugin_path)})",
            *source_lines,
            f"return {filter_name}(src{param_text})",
            "",
        ]
    )


def _render_vs_source_lines(source: dict) -> list[str]:
    if source["type"] == "blank":
        source_format = _source_value(source, "vs", "format")
        source_color = _source_value(source, "vs", "color")
        return [
            (
                "src = core.std.BlankClip("
                f"width={source['width']}, "
                f"height={source['height']}, "
                f"length={source['length']}, "
                f"format=vs.{source_format}, "
                f"color={_format_vs_value(source_color)})"
            )
        ]
    if source["type"] == "blank_sequence":
        source_format = _source_value(source, "vs", "format")
        lines = [
            "clips = [",
        ]
        for color in _source_value(source, "vs", "colors"):
            lines.append(
                "    core.std.BlankClip("
                f"width={source['width']}, "
                f"height={source['height']}, "
                "length=1, "
                f"format=vs.{source_format}, "
                f"color={_format_vs_value(color)}),"
            )
        lines.extend(
            [
                "]",
                "src = core.std.Splice(clips)",
            ]
        )
        return lines
    if source["type"] == "ffms2":
        path = json.dumps(_source_path(source))
        return [
            'if hasattr(core, "ffms2"):',
            f"    src = core.ffms2.Source(source={path})",
            'elif hasattr(core, "lsmas"):',
            f"    src = core.lsmas.LibavSMASHSource(source={path})",
            "else:",
            "    raise RuntimeError("
            "\"no supported VapourSynth source filter found; tried core.ffms2.Source "
            "and core.lsmas.LibavSMASHSource\""
            ")",
        ]
    raise ValueError(f"unsupported source type: {source['type']}")


def _render_avs_source_lines(source: dict) -> list[str]:
    if source["type"] == "blank":
        source_format = _source_value(source, "avs", "format")
        source_color_arg = _format_avs_source_color(source)
        return [
            (
                "src = BlankClip("
                f"width={source['width']}, "
                f"height={source['height']}, "
                f"length={source['length']}, "
                f"pixel_type={json.dumps(source_format)}, "
                f"{source_color_arg})"
            )
        ]
    if source["type"] == "blank_sequence":
        source_format = _source_value(source, "avs", "format")
        clips = []
        for color in _source_value(source, "avs", "colors"):
            clips.append(
                "BlankClip("
                f"width={source['width']}, "
                f"height={source['height']}, "
                "length=1, "
                f"pixel_type={json.dumps(source_format)}, "
                f"{_format_avs_color_value(color)})"
            )
        return ["src = " + " ++ ".join(clips)]
    if source["type"] == "ffms2":
        path = json.dumps(_source_path(source))
        return [
            'src = FunctionExists("FFVideoSource") '
            f"? FFVideoSource({path}) "
            f': FunctionExists("LSMASHVideoSource") '
            f"? LSMASHVideoSource({path}) "
            ': Assert(false, "no supported AviSynth source filter found; '
            'tried FFVideoSource and LSMASHVideoSource")',
        ]
    raise ValueError(f"unsupported source type: {source['type']}")


def _source_path(source: dict) -> str:
    return str(source["resolved_path"])


def _format_vs_value(value) -> str:
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, list):
        return "[" + ", ".join(_format_vs_value(item) for item in value) + "]"
    return repr(value)


def _format_avs_value(value) -> str:
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, list):
        return json.dumps(", ".join(str(item) for item in value))
    return repr(value)


def _format_avs_source_color(source: dict) -> str:
    if "avs_color_yuv" in source:
        return f"color_yuv={source['avs_color_yuv']}"
    return f"color={_format_avs_value(_source_value(source, 'avs', 'color'))}"


def _format_avs_color_value(value) -> str:
    if isinstance(value, str):
        return f"color_yuv={value}"
    return f"color={_format_avs_value(value)}"


def _source_value(source: dict, host: str, key: str):
    return source.get(f"{host}_{key}", source[key])


def host_params(case: dict, host: str) -> dict:
    params = dict(case.get("params", {}))
    params.update(case.get(f"{host}_params", {}))
    return params


def load_cases(cases_dir: Path) -> list[dict]:
    cases: list[dict] = []
    for path in sorted(cases_dir.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        cases.extend(_resolve_case_sources(payload.get("cases", []), path.parent))
    return cases


def _resolve_case_sources(cases: list[dict], base_dir: Path) -> list[dict]:
    resolved_cases = []
    for case in cases:
        resolved_case = dict(case)
        source = dict(case["source"])
        if source["type"] == "ffms2":
            source["resolved_path"] = (base_dir / source["path"]).resolve()
        resolved_case["source"] = source
        resolved_case["params"] = _resolve_param_paths(dict(case.get("params", {})), base_dir)
        if "vs_params" in case:
            resolved_case["vs_params"] = _resolve_param_paths(dict(case["vs_params"]), base_dir)
        if "avs_params" in case:
            resolved_case["avs_params"] = _resolve_param_paths(dict(case["avs_params"]), base_dir)
        resolved_cases.append(resolved_case)
    return resolved_cases


def _resolve_param_paths(params: dict, base_dir: Path) -> dict:
    for key in PATH_PARAM_KEYS:
        if key in params:
            params[key] = str((base_dir / params[key]).resolve())
    return params


def run_vs_case(case: dict, plugin_path: Path) -> list[dict]:
    import vapoursynth as vs

    core = vs.core
    plugin_key = str(plugin_path.resolve())
    if plugin_key not in _LOADED_VS_PLUGINS:
        core.std.LoadPlugin(path=plugin_key)
        _LOADED_VS_PLUGINS.add(plugin_key)

    clip = _vs_source_clip(core, vs, case["source"])
    filter_name = case.get("filter", DEFAULT_FILTER)
    namespace = getattr(core, PLUGIN_NAMESPACE)
    clip = getattr(namespace, filter_name)(clip, **host_params(case, "vs"))

    results = []
    for frame_number in case["frames"]:
        frame = clip.get_frame(frame_number)
        planes, metadata = _vs_frame_planes(frame, clip.num_frames)
        results.append(_result_record(case, "vs", frame_number, hash_planes(planes), metadata))
    return results


def _vs_source_clip(core, vs, source: dict):
    if source["type"] == "blank":
        return core.std.BlankClip(
            width=source["width"],
            height=source["height"],
            length=source["length"],
            format=getattr(vs, _source_value(source, "vs", "format")),
            color=_source_value(source, "vs", "color"),
        )
    if source["type"] == "blank_sequence":
        clips = [
            core.std.BlankClip(
                width=source["width"],
                height=source["height"],
                length=1,
                format=getattr(vs, _source_value(source, "vs", "format")),
                color=color,
            )
            for color in _source_value(source, "vs", "colors")
        ]
        return core.std.Splice(clips)
    if source["type"] == "ffms2":
        path = _source_path(source)
        if hasattr(core, "ffms2"):
            return core.ffms2.Source(source=path)
        if hasattr(core, "lsmas"):
            return core.lsmas.LibavSMASHSource(source=path)
        raise RuntimeError(
            "no supported VapourSynth source filter found; tried "
            "core.ffms2.Source and core.lsmas.LibavSMASHSource"
        )
    raise ValueError(f"unsupported source type: {source['type']}")


def run_avs_case(case: dict, plugin_path: Path, avs_dump: Path) -> list[dict]:
    source = case["source"]
    params = host_params(case, "avs")
    filter_name = case.get("filter", DEFAULT_FILTER)
    script = render_avs_script(str(plugin_path), source, filter_name, params)

    results = []
    with tempfile.TemporaryDirectory(prefix="delogohd-baseline-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        script_path = temp_dir / f"{case['id']}.avs"
        script_path.write_text(script, encoding="utf-8")

        for frame_number in case["frames"]:
            raw_path = temp_dir / f"{case['id']}-{frame_number}.bin"
            meta_path = temp_dir / f"{case['id']}-{frame_number}.json"
            subprocess.run(
                [
                    str(avs_dump),
                    "--script",
                    str(script_path),
                    "--frame",
                    str(frame_number),
                    "--raw-out",
                    str(raw_path),
                    "--meta-out",
                    str(meta_path),
                ],
                check=True,
            )
            sha256 = hashlib.sha256(raw_path.read_bytes()).hexdigest()
            metadata = json.loads(meta_path.read_text(encoding="utf-8"))
            results.append(_result_record(case, "avs", frame_number, sha256, metadata))
    return results


def _vs_frame_planes(frame, num_frames: int) -> tuple[list[PlaneBytes], dict]:
    fmt = frame.format
    planes: list[PlaneBytes] = []
    plane_metadata = []
    names = _vs_plane_names(fmt)

    for plane_index, name in enumerate(names):
        width = _plane_width(frame.width, fmt.subsampling_w, plane_index)
        height = _plane_height(frame.height, fmt.subsampling_h, plane_index)
        stride = frame.get_stride(plane_index)
        row_bytes = width * fmt.bytes_per_sample
        pointer = frame.get_read_ptr(plane_index)
        data = ctypes.string_at(pointer.value, stride * height)
        planes.append(PlaneBytes(name, width, height, fmt.bytes_per_sample, stride, data))
        plane_metadata.append(
            {
                "name": name,
                "row_bytes": row_bytes,
                "height": height,
                "stride": stride,
            }
        )

    metadata = {
        "width": frame.width,
        "height": frame.height,
        "frames": num_frames,
        "format": {
            "name": fmt.name,
            "id": fmt.id,
            "bits_per_sample": fmt.bits_per_sample,
            "bytes_per_sample": fmt.bytes_per_sample,
            "color_family": int(fmt.color_family),
            "sample_type": int(fmt.sample_type),
            "num_planes": fmt.num_planes,
            "subsampling_w": fmt.subsampling_w,
            "subsampling_h": fmt.subsampling_h,
        },
        "planes": plane_metadata,
    }
    return planes, metadata


def _vs_plane_names(fmt) -> list[str]:
    import vapoursynth as vs

    if fmt.color_family == vs.GRAY:
        return ["Y"]
    if fmt.color_family == vs.RGB:
        return ["R", "G", "B", "A"][:fmt.num_planes]
    if fmt.color_family == vs.YUV:
        return ["Y", "U", "V", "A"][:fmt.num_planes]
    raise ValueError(f"unsupported VapourSynth format for baseline hashing: {fmt.name}")


def _plane_width(width: int, subsampling_w: int, plane_index: int) -> int:
    if plane_index == 1 or plane_index == 2:
        return width >> subsampling_w
    return width


def _plane_height(height: int, subsampling_h: int, plane_index: int) -> int:
    if plane_index == 1 or plane_index == 2:
        return height >> subsampling_h
    return height


def _result_record(case: dict, host: str, frame_number: int, sha256: str, metadata: dict) -> dict:
    return {
        "case": case["id"],
        "tier": case["tier"],
        "host": host,
        "frame": frame_number,
        "source": case["source"]["id"],
        "params": _canonical_params(host_params(case, host)),
        "sha256": sha256,
        "metadata": _canonical_metadata(metadata),
    }


def _canonical_params(params: dict) -> dict:
    canonical = dict(params)
    for key in PATH_PARAM_KEYS:
        if key not in canonical:
            continue
        path = Path(canonical[key])
        try:
            canonical[key] = path.resolve().relative_to(BASELINE_DIR).as_posix()
        except ValueError:
            canonical[key] = str(path)
    return canonical


def _canonical_metadata(metadata: dict) -> dict:
    canonical = dict(metadata)
    canonical["planes"] = [
        {key: value for key, value in plane.items() if key != "stride"}
        for plane in metadata.get("planes", [])
    ]
    return canonical


def run_cases(cases: list[dict], hosts: list[str], plugin_path: Path, avs_dump: Path | None) -> list[dict]:
    results: list[dict] = []
    for case in cases:
        case_hosts = case.get("hosts", ["vs", "avs"])
        if "vs" in hosts and "vs" in case_hosts:
            results.extend(run_vs_case(case, plugin_path))
        if "avs" in hosts and "avs" in case_hosts:
            if avs_dump is None:
                raise ValueError("--avs-dump is required when running AviSynth cases")
            results.extend(run_avs_case(case, plugin_path, avs_dump))
    return results


def write_golden(path: Path, results: list[dict]) -> None:
    payload = {"schema": GOLDEN_SCHEMA, "results": results}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def verify_golden(path: Path, results: list[dict]) -> None:
    expected = json.loads(path.read_text(encoding="utf-8"))
    actual = {"schema": GOLDEN_SCHEMA, "results": results}
    if actual != expected:
        raise AssertionError(f"baseline mismatch: {path}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate or verify DelogoHD frame hash baselines.")
    parser.add_argument("command", choices=["generate", "verify"])
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--avs-dump", type=Path)
    parser.add_argument("--cases-dir", type=Path, default=Path(__file__).with_name("cases"))
    parser.add_argument("--golden", required=True, type=Path)
    parser.add_argument("--tier", default="smoke")
    parser.add_argument("--hosts", nargs="+", default=["vs", "avs"], choices=["vs", "avs"])
    args = parser.parse_args(argv)

    plugin_path = args.plugin.resolve()
    avs_dump = args.avs_dump.resolve() if args.avs_dump else None

    cases = select_cases(load_cases(args.cases_dir), args.tier)
    results = run_cases(cases, args.hosts, plugin_path, avs_dump)
    if args.command == "generate":
        write_golden(args.golden, results)
    else:
        verify_golden(args.golden, results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
