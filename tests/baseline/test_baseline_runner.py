import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.baseline import baseline_runner


class CanonicalHashTests(unittest.TestCase):
    def test_hashes_only_valid_row_bytes(self):
        planes = [
            baseline_runner.PlaneBytes(
                name="Y",
                width=3,
                height=2,
                bytes_per_sample=1,
                stride=5,
                data=b"abcXXdefYY",
            )
        ]

        result = baseline_runner.hash_planes(planes)

        self.assertEqual(result, hashlib.sha256(b"abcdef").hexdigest())

    def test_hashes_planes_in_supplied_order(self):
        planes = [
            baseline_runner.PlaneBytes("Y", 2, 1, 1, 2, b"ab"),
            baseline_runner.PlaneBytes("U", 2, 1, 1, 2, b"cd"),
            baseline_runner.PlaneBytes("V", 2, 1, 1, 2, b"ef"),
        ]

        result = baseline_runner.hash_planes(planes)

        self.assertEqual(result, hashlib.sha256(b"abcdef").hexdigest())


class CaseSelectionTests(unittest.TestCase):
    def test_filters_cases_by_tier(self):
        cases = [
            {"id": "smoke_default", "tier": "smoke"},
            {"id": "compat_yuv", "tier": "compat"},
            {"id": "full_hd", "tier": "full"},
        ]

        selected = baseline_runner.select_cases(cases, "smoke")

        self.assertEqual([case["id"] for case in selected], ["smoke_default"])

    def test_all_tier_keeps_all_cases(self):
        cases = [
            {"id": "smoke_default", "tier": "smoke"},
            {"id": "compat_yuv", "tier": "compat"},
        ]

        selected = baseline_runner.select_cases(cases, "all")

        self.assertEqual([case["id"] for case in selected], ["smoke_default", "compat_yuv"])


class ScriptRenderingTests(unittest.TestCase):
    def test_renders_vs_filter_call_with_logo_path_and_filter_name(self):
        source = {
            "type": "blank",
            "format": "YUV420P8",
            "width": 64,
            "height": 48,
            "length": 9,
            "color": [64, 96, 160],
        }
        params = {"logofile": "/tmp/test.lgd", "fadein": 3, "mono": True}

        script = baseline_runner.render_vs_script("/tmp/plugin.so", source, "AddlogoHD", params)

        self.assertIn('core.std.LoadPlugin(path="/tmp/plugin.so")', script)
        self.assertIn("core.std.BlankClip(width=64, height=48, length=9, format=vs.YUV420P8, color=[64, 96, 160])", script)
        self.assertIn('core.delogohd.AddlogoHD(src, fadein=3, logofile="/tmp/test.lgd", mono=True)', script)

    def test_renders_avs_filter_call_with_logo_path_and_filter_name(self):
        source = {
            "type": "blank",
            "format": "YV12",
            "width": 64,
            "height": 48,
            "length": 9,
            "color": 0,
            "avs_color_yuv": "$4060A0",
        }
        params = {"logofile": "/tmp/test.lgd", "fadeout": 3, "mono": True}

        script = baseline_runner.render_avs_script("/tmp/plugin.so", source, "DelogoHD", params)

        self.assertIn('LoadPlugin("/tmp/plugin.so")', script)
        self.assertIn('src = BlankClip(width=64, height=48, length=9, pixel_type="YV12", color_yuv=$4060A0)', script)
        self.assertIn('return DelogoHD(src, fadeout=3, logofile="/tmp/test.lgd", mono=true)', script)

    def test_renders_vs_blank_sequence_as_one_frame_clips(self):
        source = {
            "type": "blank_sequence",
            "format": "YUV420P8",
            "width": 640,
            "height": 180,
            "colors": [[32, 64, 96], [96, 128, 160]],
        }

        script = baseline_runner.render_vs_script("/tmp/plugin.so", source, "DelogoHD", {})

        self.assertIn("clips = [", script)
        self.assertIn("length=1, format=vs.YUV420P8, color=[32, 64, 96])", script)
        self.assertIn("length=1, format=vs.YUV420P8, color=[96, 128, 160])", script)
        self.assertIn("src = core.std.Splice(clips)", script)

    def test_renders_avs_blank_sequence_with_yuv_colors(self):
        source = {
            "type": "blank_sequence",
            "format": "YV12",
            "width": 640,
            "height": 180,
            "colors": [0, 0],
            "avs_colors": ["$204060", "$6080A0"],
        }

        script = baseline_runner.render_avs_script("/tmp/plugin.so", source, "DelogoHD", {})

        self.assertIn('BlankClip(width=640, height=180, length=1, pixel_type="YV12", color_yuv=$204060)', script)
        self.assertIn('BlankClip(width=640, height=180, length=1, pixel_type="YV12", color_yuv=$6080A0)', script)
        self.assertIn(" ++ ", script)

    def test_renders_avs_yuv_source_color_without_quoting(self):
        source = {
            "type": "blank",
            "format": "Y8",
            "width": 64,
            "height": 48,
            "length": 9,
            "color": 64,
            "avs_color_yuv": "$404040",
        }

        script = baseline_runner.render_avs_script("/tmp/plugin.so", source, "DelogoHD", {})

        self.assertIn('src = BlankClip(width=64, height=48, length=9, pixel_type="Y8", color_yuv=$404040)', script)


class VapourSynthSourceSelectionTests(unittest.TestCase):
    class SourceNamespace:
        def __init__(self, marker):
            self.marker = marker

        def Source(self, source):
            return ("ffms2", self.marker, source)

        def LibavSMASHSource(self, source):
            return ("lsmas", self.marker, source)

    def test_selects_ffms2_before_lsmas_when_both_are_available(self):
        core = type("Core", (), {})()
        core.ffms2 = self.SourceNamespace("a")
        core.lsmas = self.SourceNamespace("b")
        source = {"type": "ffms2", "resolved_path": "/tmp/source.mp4"}

        clip = baseline_runner._vs_source_clip(core, object(), source)

        self.assertEqual(clip, ("ffms2", "a", "/tmp/source.mp4"))

    def test_falls_back_to_lsmas_when_ffms2_is_not_available(self):
        core = type("Core", (), {})()
        core.lsmas = self.SourceNamespace("b")
        source = {"type": "ffms2", "resolved_path": "/tmp/source.mp4"}

        clip = baseline_runner._vs_source_clip(core, object(), source)

        self.assertEqual(clip, ("lsmas", "b", "/tmp/source.mp4"))

    def test_raises_clear_error_when_no_media_source_filter_is_available(self):
        source = {"type": "ffms2", "resolved_path": "/tmp/source.mp4"}

        with self.assertRaisesRegex(RuntimeError, "no supported VapourSynth source filter found"):
            baseline_runner._vs_source_clip(type("Core", (), {})(), object(), source)


class CaseResolutionTests(unittest.TestCase):
    def test_resolves_logofile_relative_to_case_file_directory(self):
        cases = [
            {
                "id": "fade_delogo",
                "tier": "smoke",
                "source": {"type": "blank", "width": 640, "height": 180, "length": 1, "format": "YUV420P8", "color": [64, 96, 160]},
                "params": {"logofile": "../assets/logos/test-640x180.lgd"},
            }
        ]

        base_dir = baseline_runner.Path("/repo/tests/baseline/cases")
        [case] = baseline_runner._resolve_case_sources(cases, base_dir)

        expected_path = base_dir.parent / "assets/logos/test-640x180.lgd"
        self.assertEqual(case["params"]["logofile"], str(expected_path.resolve()))

    def test_canonical_params_store_baseline_relative_logo_path(self):
        params = {
            "logofile": str(baseline_runner.BASELINE_DIR / "assets/logos/test-640x180.lgd"),
            "fadein": 3,
        }

        canonical = baseline_runner._canonical_params(params)

        self.assertEqual(canonical["logofile"], "assets/logos/test-640x180.lgd")
        self.assertEqual(canonical["fadein"], 3)


class ExecutionBackendTests(unittest.TestCase):
    def test_purec_backend_adds_opt_for_execution_only(self):
        case = {
            "params": {"logofile": "/tmp/test.lgd", "start": 0},
            "avs_params": {"fadein": 2},
        }

        params = baseline_runner.execution_params(case, "avs", "purec")

        self.assertEqual(
            params,
            {"logofile": "/tmp/test.lgd", "start": 0, "fadein": 2, "opt": 1},
        )
        self.assertEqual(
            baseline_runner.host_params(case, "avs"),
            {"logofile": "/tmp/test.lgd", "start": 0, "fadein": 2},
        )

    def test_highway_backend_removes_case_opt_override_for_execution_only(self):
        case = {
            "params": {"logofile": "/tmp/test.lgd", "opt": 1},
            "vs_params": {"fadeout": 2},
        }

        params = baseline_runner.execution_params(case, "vs", "highway")

        self.assertEqual(
            params,
            {"logofile": "/tmp/test.lgd", "fadeout": 2},
        )
        self.assertEqual(
            baseline_runner.host_params(case, "vs"),
            {"logofile": "/tmp/test.lgd", "opt": 1, "fadeout": 2},
        )


class AvisynthHostVariableIntegrationTests(unittest.TestCase):
    def test_avs_filter_with_logo_host_variables_renders_without_crashing(self):
        plugin_path = _required_file_env(self, "DELOGOHD_TEST_PLUGIN")
        avs_dump = _required_file_env(self, "DELOGOHD_TEST_AVS_DUMP")
        logo_path = baseline_runner.BASELINE_DIR / "assets/logos/test-640x180.lgd"
        script = _render_avs_host_variable_smoke_script(plugin_path, logo_path)

        with tempfile.TemporaryDirectory(prefix="delogohd-host-vars-") as temp_dir_name:
            temp_dir = Path(temp_dir_name)
            script_path = temp_dir / "host-vars.avs"
            raw_path = temp_dir / "host-vars.bin"
            meta_path = temp_dir / "host-vars.json"
            script_path.write_text(script, encoding="utf-8")

            subprocess.run(
                [
                    str(avs_dump),
                    "--script",
                    str(script_path),
                    "--frame",
                    "0",
                    "--raw-out",
                    str(raw_path),
                    "--meta-out",
                    str(meta_path),
                ],
                check=True,
            )


def _render_avs_host_variable_smoke_script(plugin_path, logo_path):
    return "\n".join(
        [
            f"LoadPlugin({json.dumps(str(plugin_path))})",
            'src = BlankClip(width=640, height=180, length=1, pixel_type="YV12", color_yuv=$4060A0)',
            f"return DelogoHD(src, logofile={json.dumps(str(logo_path))}, start=0, end=0)",
            "",
        ]
    )


def _required_file_env(test_case, name):
    value = os.environ.get(name)
    if not value:
        test_case.skipTest(f"{name} is not set")
    path = Path(value)
    if not path.exists():
        test_case.skipTest(f"{name} does not exist: {path}")
    return path.resolve()


if __name__ == "__main__":
    unittest.main()
