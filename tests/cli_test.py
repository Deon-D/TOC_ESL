"""Public CLI regression for the CHI VIP mainline runner."""
import collections
import csv
import json
import pathlib
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)

    def invoke(*args, success=True):
        result_path = root / "result.json"
        result_path.unlink(missing_ok=True)
        proc = subprocess.run(
            [binary, "--json", "result.json", "--log-level", "off", *args],
            cwd=root, capture_output=True, text=True, timeout=20)
        assert (proc.returncode == 0) == success, (args, proc.stdout, proc.stderr)
        data = json.loads(result_path.read_text())
        assert data["success"] == success
        assert data["model"] == "chi-vip-gbcore"
        if success:
            assert data["completed"] == data["loads"] + data["stores"]
            assert data["nodes"]["hn"] == data["nodes"]["sn"] == 1
            assert data["cycles"] > 0 and data["throughput"] > 0
            assert "configuration_toml" in data
        else:
            assert data["error"], data
        return data

    (root / "settings.toml").write_text(
        "[workload]\nrequests=3\nwrite_percent=50\n[clock]\nperiod_ps=2000\n"
        "[rn]\ncount=3\ncache_lines=2\nrequest_slots=2\n"
        "[hn]\nrequest_slots=2\n[sn]\nservice_latency=2\n")
    first = invoke("--requests", "12", "--config", "settings.toml", "--trace", "events.csv")
    assert first["completed"] == 12 and first["nodes"]["rn"] == 3
    assert "period_ps = 2000" in first["configuration_toml"]
    assert first["last_time_ps"] == (first["cycles"] - 1) * 2000
    with (root / "events.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert rows and set(rows[0]) == {
        "time_ps", "cycle", "model", "channel", "txn_id", "opcode", "data_id"}
    counts = collections.Counter(row["channel"] for row in rows)
    assert set(counts) <= {"REQ", "RSP", "SNP", "DAT"}
    for channel in ["req", "rsp", "snp", "dat"]:
        assert counts[channel.upper()] == first["flits"][channel]
    models = {row["model"] for row in rows}
    assert {"vip.rn0", "vip.rn1", "vip.rn2", "vip.hn", "vip.sn"} <= models
    assert all(int(row["time_ps"]) == int(row["cycle"]) * 2000 for row in rows)
    assert all(int(row["data_id"]) in (0, 2) if row["channel"] == "DAT"
               else int(row["data_id"]) == -1 for row in rows)
    second = invoke("--config", "settings.toml", "--requests", "12")
    assert first["cycles"] == second["cycles"] and first["flits"] == second["flits"]
    reverse = invoke("--config", "settings.toml", "--requests", "12",
                     "--reverse-registration", "--trace", "reverse.csv")
    assert first["cycles"] == reverse["cycles"] and first["flits"] == reverse["flits"]
    assert (root / "events.csv").read_bytes() == (root / "reverse.csv").read_bytes()
    mixed = invoke("--requests", "128", "--rn-count", "4", "--cache-lines", "1",
                   "--rn-slots", "2", "--snoop-slots", "1", "--pending-operations", "4",
                   "--hn-slots", "1", "--sn-slots", "1", "--sn-latency", "9",
                   "--timeout-cycles", "10000", "--write-percent", "60",
                   "--memory-size", "512", "--address-mode", "random", "--seed", "9")
    assert mixed["completed"] == 128 and mixed["loads"] > 0 and mixed["stores"] > 0
    assert mixed["nodes"]["rn"] == 4 and mixed["flits"]["snp"] > 0
    reads = invoke("--requests", "16", "--rn-count", "1", "--write-percent", "0")
    assert reads["loads"] == 16 and reads["stores"] == 0
    for args in [("--ddr-latency", "40"), ("--ddr-capacity", "8"),
                 ("--outstanding", "8"), ("--hn-entries", "4"), ("--sn-entries", "8"),
                 ("--depth", "4"), ("--flit-delay", "1"), ("--credit-delay", "1"),
                 ("--requests", "-1"), ("--rn-count", "0"), ("--cache-lines", "0"),
                 ("--rn-slots", "0"), ("--hn-slots", "0"), ("--sn-slots", "0"),
                 ("--start-address", "1"), ("--period-ps", "0"),
                 ("--write-percent", "101"), ("--seed", "12junk"),
                 ("--unknown", "3"), ("--max-cycles", "1")]:
        invoke(*args, success=False)
    for content in ["[hn]\nentries=2", "[clock]\nperiod_ps='1000'",
                    "[ddr]\nlatency_cycles=40", "[rn]\noutstanding=8",
                    "[sn]\nentries=8", "[chi]\ndepth=4"]:
        (root / "bad.toml").write_text(content)
        invoke("--config", "bad.toml", success=False)
    invoke("--trace", "./result.json", success=False)
print("CHI VIP CLI passed")
