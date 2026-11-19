"""Tests of esphome_matrix.py, which work without the network: python -m unittest."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import esphome_matrix as m  # noqa: E402


def releases(*names):
    return [m.parse(name) for name in names]


class ParseTest(unittest.TestCase):
    def test_reads_releases_and_skips_the_rest(self):
        self.assertEqual(m.parse("2026.11.0"), (2026, 11, 0, 3, 0))
        self.assertEqual(m.parse("2026.12.0b2"), (2026, 12, 0, 1, 2))
        self.assertIsNone(m.parse("2026.12.0.dev1"))
        self.assertIsNone(m.parse("latest"))

    def test_a_pre_release_sorts_before_its_release(self):
        self.assertLess(m.parse("2026.12.0b3"), m.parse("2026.12.0rc1"))
        self.assertLess(m.parse("2026.12.0rc1"), m.parse("2026.12.0"))

    def test_text_round_trips(self):
        for name in ("2026.9.0", "2026.12.0b1", "2027.1.3rc2"):
            self.assertEqual(m.text_of(m.parse(name)), name)


class VersionsTest(unittest.TestCase):
    ALL = releases("2026.8.4", "2026.9.0", "2026.9.1", "2026.10.0", "2026.10.1", "2026.10.2", "2026.11.0")

    def names(self, chosen):
        return [(m.text_of(v), pre) for v, pre in chosen]

    def test_oldest_release_and_the_newest_patch_of_each_series(self):
        chosen = m.versions_for(self.ALL, m.parse("2026.9.0"), ends_only=False)
        self.assertEqual(
            self.names(chosen),
            [("2026.9.0", False), ("2026.9.1", False), ("2026.10.2", False), ("2026.11.0", False)],
        )

    def test_nothing_below_the_minimum(self):
        chosen = m.versions_for(self.ALL, m.parse("2026.10.0"), ends_only=False)
        self.assertEqual(
            self.names(chosen), [("2026.10.0", False), ("2026.10.2", False), ("2026.11.0", False)]
        )

    def test_ends_only_takes_the_oldest_and_the_newest(self):
        chosen = m.versions_for(self.ALL, m.parse("2026.9.0"), ends_only=True)
        self.assertEqual(self.names(chosen), [("2026.9.0", False), ("2026.11.0", False)])

    def test_a_new_release_is_picked_up_without_a_change(self):
        newer = self.ALL + releases("2026.11.1", "2026.12.0")
        chosen = m.versions_for(newer, m.parse("2026.9.0"), ends_only=False)
        self.assertEqual(
            self.names(chosen)[-2:], [("2026.11.1", False), ("2026.12.0", False)]
        )

    def test_only_the_last_minor_series_are_built(self):
        many = releases(*[f"2026.{n}.0" for n in range(1, 13)] + ["2027.1.0"])
        chosen = m.versions_for(many, m.parse("2026.9.0"), ends_only=False)
        # the oldest release plus the newest four series (2026.10 to 2027.1)
        self.assertEqual(
            [m.text_of(v) for v, _ in chosen],
            ["2026.9.0", "2026.10.0", "2026.11.0", "2026.12.0", "2027.1.0"],
        )

    def test_a_pre_release_ahead_of_the_stable_ones_is_experimental(self):
        with_beta = self.ALL + releases("2026.12.0b1", "2026.12.0b2")
        chosen = m.versions_for(with_beta, m.parse("2026.9.0"), ends_only=False)
        self.assertEqual(self.names(chosen)[-1], ("2026.12.0b2", True))

    def test_a_pre_release_of_a_series_that_is_out_is_not_built(self):
        stale = self.ALL + releases("2026.11.0rc1")
        chosen = m.versions_for(stale, m.parse("2026.9.0"), ends_only=False)
        self.assertFalse(any(pre for _, pre in chosen))


class MatrixTest(unittest.TestCase):
    def test_the_minimums_come_from_the_project(self):
        self.assertEqual(m.text_of(m.minimum_of([m.CHIP_FILE])), "2026.9.0")
        self.assertEqual(m.text_of(m.minimum_of([m.CHIP_FILE, "features/rf.yaml"])), "2026.10.0")

    def test_every_config_is_built_and_the_pro_needs_the_newer_release(self):
        matrix = m.build_matrix(releases("2026.9.0", "2026.10.0", "2026.11.0"))
        by_config = {}
        for entry in matrix["include"]:
            by_config.setdefault(entry["config"], []).append(entry["esphome"])
        self.assertEqual(set(by_config), set(m.CONFIGS))
        self.assertEqual(by_config["ci-rm4-pro"], ["2026.10.0", "2026.11.0"])
        self.assertEqual(by_config["ci-rm4-mini"][0], "2026.9.0")
        self.assertEqual(by_config["ci-rm4-mini-mac"], ["2026.9.0", "2026.11.0"])

    def test_the_configs_exist(self):
        for config in m.CONFIGS:
            self.assertTrue((m.ROOT / ".github" / "tests" / f"{config}.yaml").is_file(), config)


if __name__ == "__main__":
    unittest.main()
