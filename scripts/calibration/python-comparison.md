# codehealth against CodeScene on Python

Corpus: pytorch 4dc1d5d, tensorflow 8d95b42, django a1f2b45 (shallow clones in
~/tmp/calibration-repos). Each repository has two samples of 50 Python files spread
over five size bands and five score bands: the first one tuned the fixes, and the
held-out one (`-holdout`, no file in common) checks them. Rerun with
`compare-python.sh` (`HOLDOUT=1` for the second sample) or, after a code change,
`rerun-python.sh "<label>"`. `python-probes.py` reviews small files that each isolate
one counting question and prints CodeScene's answer next to codehealth's.

## Before and after

Pearson r / Spearman rho / mean absolute difference / mean (codehealth - CodeScene).

| Sample | Original Python commit | After the fixes | Docstrings not code |
|---|---|---|---|
| pytorch | 0.86 / 0.75 / 0.60 / +0.39 | 0.92 / 0.92 / 0.47 / +0.20 | 0.91 / 0.87 / 0.50 / +0.24 |
| tensorflow | 0.81 / 0.75 / 0.58 / +0.43 | 0.96 / 0.97 / 0.26 / +0.01 | 0.97 / 0.98 / 0.23 / +0.06 |
| django | 0.86 / 0.73 / 0.42 / +0.27 | 0.98 / 0.83 / 0.17 / +0.00 | 0.97 / 0.82 / 0.20 / +0.03 |
| pytorch held out | 0.85 / 0.83 / 0.71 / +0.53 | 0.91 / 0.87 / 0.48 / +0.21 | 0.91 / 0.86 / 0.51 / +0.28 |
| tensorflow held out | 0.74 / 0.81 / 0.34 / +0.17 | 0.70 / 0.75 / 0.37 / +0.09 | 0.72 / 0.77 / 0.34 / +0.16 |
| django held out | 0.96 / 0.80 / 0.32 / +0.21 | 0.97 / 0.88 / 0.24 / +0.12 | 0.97 / 0.88 / 0.23 / +0.12 |

C, C++, Objective-C and TypeScript are unchanged: `check-other-languages.py` compares
all 15,933 non-Python files in the three repositories (score and every finding) before
and after, and finds no difference. `calibrate.py evaluate` gives the same numbers for
every C/C++ repository; only its Python rows move (requests Spearman 0.89 to 0.83,
flask 0.69 to 0.74, django 0.90 to 0.94).

## What changed, and the CodeScene behaviour behind it

Each rule was read off probes (`python-probes.py`) before it was changed.

1. Duplication. CodeScene's Python penalty grows with the number of cloned functions
   in the file (about 0.6 points for 2, 1.5 for 10, 2 for 20 or more) and applies only
   from about 70 lines of code. Python now scores `Code Duplication` by that count,
   with a weight and cap fitted to the probe penalties; other languages keep the
   duplicated-line percentage and its 0.28 cap.
2. Low Cohesion. Python measures LCOM4 over the whole module: every top-level function
   and method, related by shared `self` fields or by naming another function or class.
   `self.assertEqual(...)` no longer counts as a shared field. Flagged from 16
   unrelated groups in modules of 550 lines or more (500 never fired in the probes).
3. Number of Functions in a Single Module: new, Python only. Flagged at 76 top-level
   functions and methods (75 was not) in modules of 550 lines or more.
4. Bumpy Road. A bump is a run of code at nesting depth 2 or more; a top-level
   statement, or a plain statement at depth 1, ends it. Matches 13 of 15 shape probes;
   CodeScene counts an `else` holding deeper nesting as a separate bump, codehealth
   does not.
5. Deep Nesting. `try`, `with` and `match` no longer nest in Python.
6. Cyclomatic complexity. Each `except` and `raise` adds 1; `with`, `x if c else y`,
   a comprehension's `if` and `match` cases add nothing. All 17 complexity probes now
   give CodeScene's exact number.
7. Lines of code. Module, class and function docstrings are not code, like
   comments. CodeScene counts them; this is a deliberate difference
   (`docs/DeliberateDifferences.md`), so Large Method and Large File now fire later
   than CodeScene's on documented code.

Also: `Source/Report.cpp`'s `appendSection` took 5 arguments; it is now
`formatSection` with 4 and returns the text.

## Decisions, with the code that settled them

Jamie's decisions (recorded in `docs/DeliberateDifferences.md`): Bumpy Road now
counts each shallow `elif`/`else` branch that nests again as its own bump; a long
Python method's cost grows in proportion to its length (584 lines: CodeScene 7.21,
codehealth 7.21); clones stay at 90%, and the rocm miss below was a bug (common
trigrams were ignored even within one file), now fixed; Large File stays at 1000.
The examples that informed them:

### Finding costs (codehealth reuses the weights fitted on C++)

Same findings in both tools, different scores:

| File | Finding | CodeScene | codehealth | Read |
|---|---|---|---|---|
| pytorch test/dynamo/test_utils.py | `TestDynamoTimed.test_dynamo_timed` is a 584-line method, plus Low Cohesion | 5.52 | 8.11 | CodeScene. A 584-line function is a serious problem; Large Method's cap of 1.56 points lets it cost the same as a 150-line one. |
| pytorch torch/distributions/kl.py | Low Cohesion only (74 unrelated KL-divergence functions) | 7.55 | 8.16 | codehealth. The file is a registry of formulas that belong together; 2.45 points is too much for that. |
| django/db/backends/oracle/introspection.py | `get_field_type` cc 12, `get_table_description` cc 10, Overall Code Complexity | 7.68 | 8.45 | Unclear. Two moderately complex methods in 400 lines; CodeScene's 2.3 points feels heavy, ours light. |
| pytorch test/optim/test_stateless.py | One Large Method, 109 lines | 9.39 | 9.70 | CodeScene, slightly. A 109-line test is worth more than 0.3 points. |

### Clone threshold (codehealth 90%, CodeScene 75%; kept, see docs/DeliberateDifferences.md)

| File | Functions | CodeScene | codehealth | Read |
|---|---|---|---|---|
| django/utils/formats.py | `date_format`, `time_format` | clones | not clones | codehealth. Each is a three-line call to a different formatter; CodeScene reaches its 10-line minimum by counting the docstrings. |
| torch/_inductor/rocm_multiarch_utils.py | `get_rocm_compiler`, `get_rocm_bundler` | clones | not clones | CodeScene. Copy and paste with a different path and message; one helper taking the tool name would replace both. |
| django tests/forms_tests/field_tests/test_charfield.py | `test_charfield_4`, `test_charfield_5` | clones | not clones | CodeScene, weakly. Same assertions on one field with `required` flipped; a shared helper would read better, but tests often repeat on purpose. |
| django/core/checks/security/sessions.py | `check_session_cookie_secure`, `check_session_cookie_httponly` | not clones | clones | codehealth. Identical checks differing only in the setting and warnings; CodeScene skips them because each is under 10 lines. |

### Large File (codehealth 1000 lines of code, CodeScene 600 for Python)

| File | Lines of code | CodeScene | codehealth | Read |
|---|---|---|---|---|
| django/urls/resolvers.py | 667 | Large File | no finding | codehealth. Nine classes that together are URL resolution; splitting it would scatter one concept. |
| torchgen/api/python.py | 893 | Large File | no finding | CodeScene. A grab bag of signature and argument helpers with no centre; it would be easier to work in split. |
| torch/ao/quantization/fx/_model_report/detector.py | 806 | Large File | no finding | CodeScene. Several unrelated detector classes in one file; each could stand alone. |
| django/contrib/admin/options.py | 2158 | Number of Functions, no Large File | Large File | Both right. CodeScene reports one module-size smell and stops; codehealth reports both the size and the function count. |

### Bumpy Road (codehealth: a plain statement at depth 1 ends a bump)

| File | Function | CodeScene | codehealth | Read |
|---|---|---|---|---|
| torch/distributed/tensor/parallel/loss.py | `_cast_to_dtensor` | 2 bumps | 1 | CodeScene. The `if` and `elif` branches each hold their own nested decision; they are two bumps. codehealth merges an if/elif chain into one. |
| torch/testing/_internal/common_methods_invocations.py | `sample_inputs_linspace_tensor_overload` | below 2 | 2 | CodeScene. One loop of flat guards; an assignment between them should not split it into two bumps. |
| torchgen/api/python.py | `argument_type_str` | 5 bumps | 2 | CodeScene. A long if/elif chain whose branches each nest again; same cause as `_cast_to_dtensor`. |
| torch/profiler/_utils.py | `map_recorded_events_to_aten_ops_with_stack_trace` | 10 bumps | 5 | CodeScene. Same cause: branches of one chain counted once. |

My read: CodeScene counts each branch of an if/elif/else that holds nested logic as its
own bump, and codehealth should too; the "plain statement ends a bump" rule is the
weaker half of the current Python rule.

## Still off

- Low Cohesion finds 5 of 9 in the held-out sample. Test classes whose methods all call
  one helper count as one group here and as low cohesion in CodeScene.
- Overall Code Complexity fires about twice as often as CodeScene's (weight 0.001, so
  the score barely moves). Primitive Obsession fires on files CodeScene leaves alone.

<!-- Everything below is written by compare-python.py report. -->

CodeScene's numbers come from its MCP server's `code_health_review`; codehealth's from `codehealth --json` on the whole checkout.

## History

One row per run of `compare-python.sh`, newest last.

| Run | pytorch r / rho / MAD / bias | tensorflow r / rho / MAD / bias | django r / rho / MAD / bias |
|---|---|---|---|
| 0 before (Report.cpp fix only) | 0.86 / 0.75 / 0.60 / +0.39 | 0.81 / 0.75 / 0.58 / +0.43 | 0.86 / 0.73 / 0.42 / +0.27 |
| 1 duplication cap 2.0 for Python | 0.88 / 0.77 / 0.55 / +0.31 | 0.81 / 0.78 / 0.62 / +0.00 | 0.87 / 0.59 / 0.46 / -0.04 |
| 1 duplication: cloned-function count, 70-line gate, fitted weight and cap | 0.88 / 0.79 / 0.57 / +0.35 | 0.91 / 0.92 / 0.39 / +0.24 | 0.92 / 0.75 / 0.34 / +0.16 |
| 2+3 Low Cohesion per module, Number of Functions in a Single Module | 0.93 / 0.91 / 0.45 / +0.16 | 0.96 / 0.92 / 0.29 / -0.02 | 0.98 / 0.77 / 0.22 / -0.03 |
| 5 Deep Nesting: try, with and match do not nest | 0.93 / 0.93 / 0.46 / +0.22 | 0.97 / 0.96 / 0.26 / +0.01 | 0.98 / 0.79 / 0.20 / -0.00 |
| 6 Complex Method: except and raise count; with, conditional expressions, comprehension if and case do not | 0.92 / 0.92 / 0.48 / +0.24 | 0.96 / 0.97 / 0.25 / +0.03 | 0.98 / 0.83 / 0.17 / +0.01 |
| 4 Bumpy Road: each nested block is a bump | 0.93 / 0.92 / 0.41 / +0.11 | 0.96 / 0.97 / 0.26 / -0.03 | 0.98 / 0.81 / 0.18 / -0.02 |
| 4 Bumpy Road: each if inside another if is a bump; loops do not count | 0.93 / 0.92 / 0.45 / +0.19 | 0.96 / 0.97 / 0.27 / +0.02 | 0.98 / 0.83 / 0.18 / +0.02 |
| 4 Bumpy Road: runs of deep code split by shallow statements | 0.92 / 0.91 / 0.47 / +0.20 | 0.96 / 0.97 / 0.26 / +0.01 | 0.98 / 0.83 / 0.17 / +0.00 |
| 4 Bumpy Road: runs of deep code; any top-level statement or shallow plain statement ends one | 0.92 / 0.92 / 0.47 / +0.20 | 0.96 / 0.97 / 0.26 / +0.01 | 0.98 / 0.83 / 0.17 / +0.00 |
| 7 blank lines inside docstrings are not code | 0.92 / 0.92 / 0.47 / +0.20 | 0.96 / 0.97 / 0.26 / +0.01 | 0.98 / 0.83 / 0.17 / +0.00 |
| 8 after all fixes, with a held-out sample | 0.92 / 0.92 / 0.47 / +0.20 | 0.96 / 0.97 / 0.26 / +0.01 | 0.98 / 0.83 / 0.17 / +0.00 |
| 8 held-out sample, original Python commit (before) and after all fixes | - | - | - |
| 9 docstrings are not code (deliberate difference) | 0.91 / 0.87 / 0.50 / +0.24 | 0.97 / 0.98 / 0.23 / +0.06 | 0.97 / 0.82 / 0.20 / +0.03 |
| 10 elif/else branches are bumps, linear Large Method cost, same-file clones in large repos | 0.94 / 0.94 / 0.42 / +0.10 | 0.97 / 0.97 / 0.24 / -0.07 | 0.97 / 0.78 / 0.24 / -0.12 |

## pytorch

Whole repository: exit code 0, 5.42 s wall clock, 5131 of 5134 tracked .py files scored.

Not scored (3; codehealth skips third_party, vendor and similar directories by design): third_party/generate-cpuinfo-wrappers.py, third_party/generate-xnnpack-wrappers.py, torch/testing/_internal/generated/__init__.py

Sample: 48 files compared, 2 without a CodeScene score, 0 failed.

| Pearson | Spearman | Mean absolute difference | Mean (codehealth - CodeScene) |
|---|---|---|---|
| 0.94 | 0.94 | 0.42 | +0.10 |

CodeScene gave no score (empty or declaration-only files):

- tools/__init__.py (codehealth 10.00, 1 lines)
- test/quantization/bc/__init__.py (codehealth 10.00, 1 lines)

Findings by type (a CodeScene function finding matched by one of ours in the same function):

| Type | Matched | Missed | Extra | Recall | Precision |
|---|---|---|---|---|---|
| Excess Arguments | 360 | 0 | 4 | 100% | 99% |
| Complex Method | 73 | 21 | 1 | 78% | 99% |
| Bumpy Road | 51 | 9 | 20 | 85% | 72% |
| Complex Conditional | 45 | 9 | 0 | 83% | 100% |
| Large Method | 23 | 8 | 0 | 74% | 100% |
| Overall Code Complexity | 6 | 2 | 12 | 75% | 33% |
| Deep Nesting | 14 | 0 | 0 | 100% | 100% |
| Low Cohesion | 4 | 3 | 1 | 57% | 80% |
| Code Duplication | 4 | 2 | 1 | 67% | 80% |
| Large File | 0 | 2 | 4 | 0% | 0% |
| Primitive Obsession | 0 | 0 | 5 | - | 0% |
| Number of Functions in a Single Module | 3 | 0 | 1 | 100% | 75% |
| Brain Method | 0 | 1 | 1 | 0% | 0% |
| Lines of Declarations in a Single File | 0 | 1 | 0 | 0% | - |

Worst 10 files by score difference:

| File | Lines | CodeScene | codehealth | Missed | Extra |
|---|---|---|---|---|---|
| test/jit/test_pdt.py | 799 | 6.59 | 9.16 | Low Cohesion :0 (); Code Duplication TestPDT.test_nested_nn_module_class:54 (None); Large Method TestPDT.test_multiple_class_with_same_method:276 (LoC = 72 lines) |  |
| torch/nn/modules/activation.py | 682 | 6.37 | 4.40 | Complex Conditional MultiheadAttention.forward:1388 (2 complex conditional expressions) | Bumpy Road merge_masks:1525 (2); Low Cohesion :0 (30) |
| test/package/test_package_script.py | 498 | 7.55 | 9.33 | Low Cohesion :0 () |  |
| torch/_functorch/_aot_autograd/input_output_analysis.py | 396 | 7.45 | 5.83 |  | Bumpy Road create_graph_signature:431 (3); Overall Code Complexity :0 (13.833333333333334) |
| torch/distributed/fsdp/_flat_param.py | 1868 | 2.47 | 1.00 | Low Cohesion :0 (); Bumpy Road FlatParamHandle._writeback_tensor:2414 (bumps = 2); Complex Method FlatParamHandle.prepare_gradient_for_optim:1679 (cc = 13); Complex Method FlatParamHandle._writeback_tensor:2414 (cc = 9) ... | Bumpy Road flatten_tensors:838 (2); Large File :0 (1868) |
| torch/_inductor/autoheuristic/artifacts/_PadMMH200.py | 161 | 6.06 | 7.46 |  | Brain Method get_best_choices:45 (44) |
| test/inductor/test_inplacing_pass.py | 695 | 7.36 | 8.76 | Large File :0 (); Complex Method TestReinplacingPassCorrectness.test_with_effects_reinplace_list_arg:841 (cc = 9); Large Method TestReinplacingPassCorrectness.test_with_effects_reinplace:636 (LoC = 72 lines); Large Method TestReinplacingPassCorrectness.test_with_effects_reinplace_multiple_mutated_args:739 (LoC = 70 lines) ... |  |
| test/dynamo/test_exceptions.py | 1734 | 5.66 | 6.52 | Bumpy Road ExceptionTests.test_raise_match:617 (bumps = 2); Complex Method ExceptionTests.test_builtin_arg_count_type_errors:414 (cc = 21); Complex Method ExceptionTests.test_raise_match:617 (cc = 14); Large Method ExceptionTests.test_builtin_arg_count_type_errors:414 (LoC = 75 lines) | Large File :0 (1734) |
| torch/onnx/_internal/exporter/_registration.py | 179 | 8.84 | 9.65 | Overall Code Complexity :0 (); Complex Method OnnxDecompMeta.__post_init__:61 (cc = 9) |  |
| torch/profiler/_utils.py | 143 | 7.65 | 8.42 |  | Overall Code Complexity :0 (7.166666666666667) |

## tensorflow

Whole repository: exit code 0, 6.11 s wall clock, 3199 of 3343 tracked .py files scored.

Not scored (144; codehealth skips third_party, vendor and similar directories by design): tensorflow/tools/build_info/gen_build_info.py, third_party/__init__.py, third_party/xla/.github/workflows/github_api.py, third_party/xla/.github/workflows/pr_size_check.py, third_party/xla/.github/workflows/rollback_notification.py, third_party/xla/build_tools/ci/bazel_diff.py, third_party/xla/build_tools/ci/bazel_diff_test.py, third_party/xla/build_tools/ci/build.py, third_party/xla/build_tools/ci/build_test.py, third_party/xla/build_tools/ci/change_detector.py

Sample: 49 files compared, 1 without a CodeScene score, 0 failed.

| Pearson | Spearman | Mean absolute difference | Mean (codehealth - CodeScene) |
|---|---|---|---|
| 0.97 | 0.97 | 0.24 | -0.07 |

CodeScene gave no score (empty or declaration-only files):

- tensorflow/python/ops/sets.py (codehealth 10.00, 5 lines)

Findings by type (a CodeScene function finding matched by one of ours in the same function):

| Type | Matched | Missed | Extra | Recall | Precision |
|---|---|---|---|---|---|
| Excess Arguments | 86 | 0 | 0 | 100% | 100% |
| Complex Method | 37 | 9 | 2 | 80% | 95% |
| Large Method | 6 | 27 | 0 | 18% | 100% |
| Bumpy Road | 15 | 5 | 4 | 75% | 79% |
| Complex Conditional | 16 | 5 | 0 | 76% | 100% |
| Code Duplication | 14 | 1 | 1 | 93% | 93% |
| Overall Code Complexity | 3 | 1 | 7 | 75% | 30% |
| Deep Nesting | 9 | 0 | 0 | 100% | 100% |
| Low Cohesion | 6 | 0 | 2 | 100% | 75% |
| Large File | 1 | 0 | 4 | 100% | 20% |
| Number of Functions in a Single Module | 2 | 0 | 1 | 100% | 67% |
| Primitive Obsession | 0 | 0 | 2 | - | 0% |
| Brain Method | 1 | 0 | 0 | 100% | 100% |
| Brain Class | 0 | 0 | 1 | - | 0% |

Worst 10 files by score difference:

| File | Lines | CodeScene | codehealth | Missed | Extra |
|---|---|---|---|---|---|
| tensorflow/python/autograph/operators/control_flow_test.py | 1076 | 8.03 | 6.54 |  | Low Cohesion :0 (47) |
| tensorflow/python/kernel_tests/data_structures/barrier_ops_test.py | 662 | 5.57 | 6.66 | Bumpy Road BarrierTest._testParallelPartialInsertManyTakeManyCloseHalfwayThrough:659 (bumps = 2); Overall Code Complexity :0 (); Large Method BarrierTest._testParallelPartialInsertManyTakeManyCloseHalfwayThrough:659 (LoC = 81 lines) |  |
| tensorflow/python/framework/graph_util_test.py | 409 | 8.73 | 7.86 |  |  |
| tensorflow/python/framework/graph_to_function_def.py | 109 | 7.62 | 8.26 | Bumpy Road _create_input_dict:63 (bumps = 3) |  |
| tensorflow/python/kernel_tests/control_flow/while_v2_test.py | 1724 | 4.98 | 4.35 |  | Large File :0 (1724) |
| tensorflow/python/distribute/distribute_lib.py | 1420 | 4.76 | 5.37 | Bumpy Road _CurrentDistributionContext.__exit__:768 (bumps = 3); Complex Method StrategyExtendedV2._scope:2491 (cc = 11); Complex Method ReplicaContextBase.all_reduce:3546 (cc = 10); Large Method StrategyBase.reduce:1675 (LoC = 144 lines) ... | Complex Method reduce.mean_reduce_helper.dimension:1810 (9); Bumpy Road __enter__:750 (2); Bumpy Road reduce.mean_reduce_helper.dimension:1810 (2); Large File :0 (1420) ... |
| tensorflow/tools/compatibility/tf_upgrade_v2_main.py | 170 | 9.05 | 8.46 |  | Overall Code Complexity :0 (12.5) |
| tensorflow/python/ops/array_ops.py | 1990 | 1.57 | 1.00 | Bumpy Road _get_dtype_from_nested_lists:1266 (bumps = 2); Bumpy Road one_hot:4028 (bumps = 2); Complex Method sparse_placeholder:3174 (cc = 10); Complex Method sequence_mask:4211 (cc = 9) ... | Complex Method fold:6419 (55); Large File :0 (1990); Code Duplication :0 (8); Brain Class :0 (113) ... |
| tensorflow/python/ops/linalg/linear_operator_permutation.py | 135 | 8.81 | 9.34 | Code Duplication LinearOperatorPermutation._to_dense:245 (None) |  |
| tensorflow/dtensor/python/tests/multi_client_input_util_test.py | 398 | 8.39 | 7.91 |  |  |

## django

Whole repository: exit code 0, 0.79 s wall clock, 2943 of 2943 tracked .py files scored.

Sample: 39 files compared, 11 without a CodeScene score, 0 failed.

| Pearson | Spearman | Mean absolute difference | Mean (codehealth - CodeScene) |
|---|---|---|---|
| 0.97 | 0.78 | 0.24 | -0.12 |

CodeScene gave no score (empty or declaration-only files):

- tests/test_utils/__init__.py (codehealth 10.00, 1 lines)
- django/contrib/postgres/forms/__init__.py (codehealth 10.00, 3 lines)
- tests/force_insert_update/__init__.py (codehealth 10.00, 1 lines)
- tests/i18n/project_dir/app_no_locale/__init__.py (codehealth 10.00, 1 lines)
- tests/base/models.py (codehealth 10.00, 4 lines)
- tests/dbshell/__init__.py (codehealth 10.00, 1 lines)
- tests/shortcuts/__init__.py (codehealth 10.00, 1 lines)
- tests/migrations/migrations_test_apps/conflicting_app_with_dependencies/migrations/0002_second.py (codehealth 10.00, 17 lines)
- tests/template_backends/apps/importerror/__init__.py (codehealth 10.00, 1 lines)
- tests/urlpatterns_reverse/erroneous_urls.py (codehealth 10.00, 5 lines)
- django/contrib/gis/geos/prototypes/geom.py (codehealth 10.00, 48 lines)

Findings by type (a CodeScene function finding matched by one of ours in the same function):

| Type | Matched | Missed | Extra | Recall | Precision |
|---|---|---|---|---|---|
| Complex Method | 24 | 1 | 0 | 96% | 100% |
| Complex Conditional | 18 | 1 | 0 | 95% | 100% |
| Bumpy Road | 13 | 5 | 0 | 72% | 100% |
| Large Method | 13 | 1 | 0 | 93% | 100% |
| Excess Arguments | 13 | 0 | 0 | 100% | 100% |
| Code Duplication | 3 | 1 | 5 | 75% | 38% |
| Overall Code Complexity | 3 | 0 | 5 | 100% | 38% |
| Deep Nesting | 7 | 0 | 0 | 100% | 100% |
| Low Cohesion | 4 | 0 | 0 | 100% | 100% |
| Number of Functions in a Single Module | 3 | 0 | 0 | 100% | 100% |
| Large File | 0 | 0 | 3 | - | 0% |
| Brain Method | 0 | 0 | 1 | - | 0% |
| Brain Class | 0 | 0 | 1 | - | 0% |

Worst 10 files by score difference:

| File | Lines | CodeScene | codehealth | Missed | Extra |
|---|---|---|---|---|---|
| tests/admin_views/tests.py | 7693 | 4.29 | 1.23 | Large Method AdminViewListEditable.test_non_field_errors:4943 (LoC = 70 lines) | Large File :0 (7693); Code Duplication :0 (59) |
| tests/template_tests/test_callables.py | 117 | 10.00 | 8.95 |  | Code Duplication :0 (4) |
| django/db/models/query.py | 2185 | 1.98 | 1.00 | Bumpy Road prefetch_one_level:2921 (bumps = 4); Bumpy Road RawModelIterable.__iter__:216 (bumps = 2); Bumpy Road QuerySet._prepare_for_bulk_create:791 (bumps = 2); Bumpy Road QuerySet.update:1436 (bumps = 2) ... | Large File :0 (2185); Code Duplication :0 (2); Brain Method prefetch_related_objects:2688 (29); Brain Class :0 (174) |
| django/core/checks/security/sessions.py | 78 | 10.00 | 9.33 |  | Code Duplication :0 (2) |
| tests/test_runner/test_parallel.py | 277 | 10.00 | 9.33 |  | Code Duplication :0 (2) |
| tests/contenttypes_tests/test_models.py | 276 | 9.38 | 10.00 | Code Duplication ContentTypesTests.test_get_for_models_creation:49 (None) |  |
| tests/get_or_create/tests.py | 560 | 6.69 | 7.27 |  |  |
| tests/generic_relations/tests.py | 700 | 6.89 | 7.37 |  |  |
| scripts/pr_quality/tests/test_check_pr.py | 1200 | 6.49 | 6.04 |  | Large File :0 (1200) |
| django/contrib/auth/management/__init__.py | 214 | 7.52 | 7.72 |  |  |
