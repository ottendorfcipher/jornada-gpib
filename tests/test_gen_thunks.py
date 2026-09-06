"""Unit tests for tools/gen_thunks.py: the GCC <-> Microsoft SH-3 calling-convention thunks."""
from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import gen_thunks as gt  # noqa: E402

HOME = gt.HOME_AREA


def _loads_stores(text: str) -> list[tuple[int, int]]:
    """Return (src, dst) displacement pairs of the r15-relative copies in a thunk body.

    Recognises both encodings the generator emits: the direct `@(disp,r15)` form and the
    scratch-base form `mov r15,r1 / add #disp,r1 / mov.l @r1,r0` (or the store equivalent).
    """
    pairs = []
    lines = [l.strip() for l in text.splitlines()]
    i = 0

    def load_at(k: int) -> tuple[int, int] | None:
        m = re.match(r"mov\.l\s+@\((\d+),r15\),r0$", lines[k])
        if m:
            return int(m.group(1)), k + 1
        if lines[k] == "mov\tr15,r1" and (m := re.match(r"add\s+#(\d+),r1$", lines[k + 1])) \
                and lines[k + 2] == "mov.l\t@r1,r0":
            return int(m.group(1)), k + 3
        return None

    def store_at(k: int) -> tuple[int, int] | None:
        m = re.match(r"mov\.l\s+r0,@\((\d+),r15\)$", lines[k])
        if m:
            return int(m.group(1)), k + 1
        if lines[k] == "mov\tr15,r1" and (m := re.match(r"add\s+#(\d+),r1$", lines[k + 1])) \
                and lines[k + 2] == "mov.l\tr0,@r1":
            return int(m.group(1)), k + 3
        return None

    while i < len(lines):
        ld = load_at(i)
        if ld is None:
            i += 1
            continue
        src, i = ld
        st = store_at(i)
        assert st, f"store must follow load: {lines[i]}"
        dst, i = st
        pairs.append((src, dst))
    return pairs


def _frame(text: str) -> int:
    m = re.search(r"add\s+#-(\d+),r15", text)
    assert m, "thunk must open a frame"
    close = re.search(r"add\s+#(\d+),r15", text)
    assert close and close.group(1) == m.group(1), "frame must be closed with the same size"
    return int(m.group(1))


@pytest.mark.unit
def test_parse_table_accepts_comments_and_blank_lines():
    entries = gt.parse_table(["# comment", "", "CreateThread 6  # six args", "Sleep 1"])
    assert [(e.name, e.nargs) for e in entries] == [("CreateThread", 6), ("Sleep", 1)]
    assert entries[0].stack_words == 2
    assert entries[1].stack_words == 0


@pytest.mark.unit
@pytest.mark.parametrize(
    "bad",
    ["CreateThread", "CreateThread six", "Create-Thread 2", "1abc 2", "X -1", "X 99",
     "A 1\nA 2"],
)
def test_parse_table_rejects_bad_lines(bad):
    with pytest.raises(ValueError):
        gt.parse_table(bad.split("\n"))


@pytest.mark.unit
def test_import_thunk_with_register_args_only_still_reserves_home_area():
    text = gt.import_thunk(gt.Entry("Sleep", 1))
    assert "_xt_Sleep:" in text
    assert _frame(text) == HOME
    assert _loads_stores(text) == []
    assert "__imp__Sleep" in text
    assert "sts.l\tpr,@-r15" in text and "lds.l\t@r15+,pr" in text


@pytest.mark.unit
def test_import_thunk_moves_stack_words_to_microsoft_slots():
    # CreateThread(6): GCC caller has words at [sp+0],[sp+4]; after PR push + frame they are
    # at frame+4 and frame+8 and must land at 16 and 20.
    text = gt.import_thunk(gt.Entry("CreateThread", 6))
    frame = _frame(text)
    assert frame == HOME + 8
    assert _loads_stores(text) == [(frame + 4, HOME), (frame + 8, HOME + 4)]


@pytest.mark.unit
def test_import_thunk_nine_args_fits_displacement_encoding():
    text = gt.import_thunk(gt.Entry("RegCreateKeyExW", 9))
    frame = _frame(text)
    pairs = _loads_stores(text)
    assert frame == HOME + 20
    assert [dst for _, dst in pairs] == [HOME + 4 * i for i in range(5)]
    assert all(src <= gt.MAX_DISP_LONG for src, _ in pairs)


@pytest.mark.unit
def test_import_thunk_large_arg_count_uses_scratch_base():
    text = gt.import_thunk(gt.Entry("Big", 13))
    assert "mov\tr15,r1" in text and "mov.l\t@r1,r0" in text


@pytest.mark.unit
def test_export_thunk_without_stack_words_is_a_plain_jump():
    text = gt.export_thunk(gt.Entry("GPB_Init", 1))
    assert "_GPB_Init:" in text and "jmp\t@r0" in text and "_GPB_Init_impl" in text
    assert "sts.l" not in text


@pytest.mark.unit
def test_export_thunk_pulls_microsoft_stack_words_down():
    # XXX_IOControl(7): Microsoft words at sp+16.. become GCC words at sp+0..
    text = gt.export_thunk(gt.Entry("GPB_IOControl", 7))
    frame = _frame(text)
    assert frame == 12
    assert _loads_stores(text) == [(frame + 4 + HOME + 4 * i, 4 * i) for i in range(3)]
    assert "_GPB_IOControl_impl" in text


@pytest.mark.unit
@pytest.mark.parametrize("n", range(0, gt.MAX_CALL_ARGS + 1))
def test_call_thunk_register_shuffle_and_stack_layout(n):
    text = gt.call_thunk(n)
    frame = _frame(text)
    assert frame == HOME + 4 * max(0, n - 4)
    body = text.splitlines()
    shuffle = [l.strip() for l in body if re.match(r"\s*mov\tr[4-7],r[0-6]$", l)]
    assert shuffle == ["mov\tr4,r0", "mov\tr5,r4", "mov\tr6,r5", "mov\tr7,r6"]
    if n >= 4:
        assert f"mov.l\t@({frame + 4},r15),r7" in text
    else:
        assert ",r7" not in text.replace("mov\tr7,r6", "")
    pairs = _loads_stores(text)
    assert pairs == [(frame + 4 + 4 * (j + 1), HOME + 4 * j) for j in range(max(0, n - 4))]
    # the copies use r0/r1 only and must all precede the register shuffle, which reuses r0
    first_shuffle = text.index("mov\tr4,r0")
    last_copy = max((text.rfind(f"@({dst},r15)") for _, dst in pairs), default=-1)
    assert last_copy < first_shuffle


@pytest.mark.unit
def test_render_kinds_and_headers():
    entries = gt.parse_table(["Sleep 1"])
    assert "_xt_Sleep" in gt.render("imports", entries)
    assert "_Sleep_impl" in gt.render("exports", entries)
    calls = gt.render("calls", [])
    assert all(f"_xt_call{n}:" in calls for n in range(gt.MAX_CALL_ARGS + 1))
    with pytest.raises(ValueError):
        gt.render("bogus", entries)


@pytest.mark.unit
def test_cli(tmp_path, capsys):
    table = tmp_path / "t.txt"
    table.write_text("Sleep 1\n")
    assert gt.main(["gen_thunks.py", "imports", str(table)]) == 0
    assert "_xt_Sleep" in capsys.readouterr().out
    assert gt.main(["gen_thunks.py", "calls"]) == 0
    assert gt.main(["gen_thunks.py"]) == 2
    table.write_text("bad line here\n")
    assert gt.main(["gen_thunks.py", "imports", str(table)]) == 1
    assert gt.main(["gen_thunks.py", "imports", str(tmp_path / "missing.txt")]) == 1
