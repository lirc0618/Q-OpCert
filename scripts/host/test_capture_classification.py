from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from flash_and_capture import CSV_HEADER, available_artifact_stem, classify_lines
from run_campaign import existing_capture_status, select_ids


def row(trial: int, *, error: int = 0, crash_magic: int = 0, crash_count: int = 0) -> str:
    return f"fw,IS-TEST,10,{trial},652,{int(error > 0)},{int(error == 0)},{error},{crash_magic},{crash_count}"


class CaptureClassificationTests(unittest.TestCase):
    def test_complete_requires_128_unique_trials_and_end(self) -> None:
        result, normalized = classify_lines([CSV_HEADER, *(row(i) for i in range(128)), "END,0"])
        self.assertEqual(result.status, "COMPLETE")
        self.assertEqual(result.unique_trials, 128)
        self.assertEqual(len(normalized), 129)

    def test_header_only_is_hang(self) -> None:
        result, normalized = classify_lines([CSV_HEADER])
        self.assertEqual(result.status, "HANG")
        self.assertEqual(normalized, [CSV_HEADER])

    def test_prefixed_uart_noise_before_header_is_accepted(self) -> None:
        result, _ = classify_lines(["noise" + CSV_HEADER, *(row(i) for i in range(128)), "END,0"])
        self.assertEqual(result.status, "COMPLETE")

    def test_missing_end_is_partial(self) -> None:
        result, _ = classify_lines([CSV_HEADER, *(row(i) for i in range(128))])
        self.assertEqual(result.status, "PARTIAL")

    def test_duplicate_trials_are_partial(self) -> None:
        result, _ = classify_lines([CSV_HEADER, *(row(0) for _ in range(128)), "END,0"])
        self.assertEqual(result.status, "PARTIAL")

    def test_hardfault_marker_takes_precedence(self) -> None:
        result, _ = classify_lines([CSV_HEADER, row(0, crash_magic=0x514F5046, crash_count=1)])
        self.assertEqual(result.status, "HARDFAULT")

    def test_no_header_is_no_boot(self) -> None:
        result, normalized = classify_lines(["boot noise"])
        self.assertEqual(result.status, "NO_BOOT")
        self.assertEqual(normalized, [])

    def test_fused_cmsis_header_is_supported(self) -> None:
        header = "firmware_id,mode,trial,cycles,status,syndrome,checks,abft_syndrome,abft_checks,accepted,max_abs_error,checksum,crash_magic,crash_count"
        rows = [f"fw,30,{i},100,0,0,0,0,0,1,0,123,0,0" for i in range(128)]
        result, normalized = classify_lines([header, *rows, "END,0"])
        self.assertEqual(result.status, "COMPLETE")
        self.assertEqual(normalized[0], header)

    def test_campaign_recognizes_terminal_status_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "mutant_deadbeef.status.json"
            path.write_text(json.dumps({"status": "HANG"}), encoding="utf-8")
            self.assertEqual(existing_capture_status(Path(tmp), "mutant"), "HANG")

    def test_existing_artifacts_get_a_new_stem(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / "mutant_deadbeef.log").write_text("old", encoding="utf-8")
            self.assertNotEqual(available_artifact_stem(out, "mutant_deadbeef"), "mutant_deadbeef")

    def test_requested_ids_preserve_user_order(self) -> None:
        items = [{"mutant_id": "IS-0001"}, {"mutant_id": "IS-0002"}]
        selected = select_ids(items, ["IS-0002", "IS-0001"])
        self.assertEqual([item["mutant_id"] for item in selected], ["IS-0002", "IS-0001"])

    def test_unknown_requested_id_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            select_ids([{"mutant_id": "IS-0001"}], ["IS-9999"])


if __name__ == "__main__":
    unittest.main()
