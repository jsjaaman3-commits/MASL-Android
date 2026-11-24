package com.mahdi.masl;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final String SOURCE_FILE = "current-project.masl";
    private static final String BYTECODE_FILE = "current-project.masb";
    private static final String SAMPLE =
            "masl 0.1;\n\n" +
            "model ExecutableFunctions {\n" +
            "    fn answer() -> i32 {\n" +
            "        return 6 * 7;\n" +
            "    }\n\n" +
            "    fn twice(value: i32) -> i32 {\n" +
            "        let result: i32 = value * 2;\n" +
            "        return result;\n" +
            "    }\n\n" +
            "    fn choose(value: i32) -> i32 {\n" +
            "        if value > 0 {\n" +
            "            return twice(value);\n" +
            "        } else {\n" +
            "            return 0;\n" +
            "        }\n" +
            "    }\n\n" +
            "    fn sum_to() -> u64 {\n" +
            "        let total: u64 = 0;\n" +
            "        repeat 10 as i {\n" +
            "            set total = total + i;\n" +
            "        }\n" +
            "        return total;\n" +
            "    }\n" +
            "}\n";

    private static final boolean NATIVE_AVAILABLE;
    private static final String NATIVE_LOAD_ERROR;

    static {
        boolean loaded = false;
        String loadError = "";
        try {
            System.loadLibrary("masl_jni");
            loaded = true;
        } catch (UnsatisfiedLinkError error) {
            loadError = error.getMessage() == null
                    ? "Unknown native library loading error"
                    : error.getMessage();
        }
        NATIVE_AVAILABLE = loaded;
        NATIVE_LOAD_ERROR = loadError;
    }

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private EditText sourceEditor;
    private EditText functionEditor;
    private EditText argumentsEditor;
    private TextView resultView;
    private TextView statusView;
    private EditText cpuInstructionEditor;
    private EditText gpuInstructionEditor;
    private EditText gpuThreadEditor;
    private Spinner memoryPatternSpinner;
    private final List<Button> actionButtons = new ArrayList<>();

    private native String nativeVersion();
    private native String nativeAnalyze(String source, String action);
    private native byte[] nativeCompile(String source);
    private native String nativeRun(byte[] bytecode, String function,
                                    String[] arguments);
    private native String nativeSimulate(String engine, long work, long threads,
                                         long seed, String memoryPattern);

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(16, 24, 39));
        getWindow().setNavigationBarColor(Color.rgb(16, 24, 39));
        buildInterface();
        loadInitialSource();
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView text(String value, float size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private LinearLayout row() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        return layout;
    }

    private Button button(String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(view -> action.run());
        actionButtons.add(button);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                0, dp(48), 1f);
        params.setMargins(dp(3), dp(3), dp(3), dp(3));
        button.setLayoutParams(params);
        return button;
    }

    private void addRow(LinearLayout parent, Button... buttons) {
        LinearLayout line = row();
        for (Button button : buttons) {
            line.addView(button);
        }
        parent.addView(line, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
    }

    private EditText numberEditor(String value, String hint) {
        EditText editor = new EditText(this);
        editor.setSingleLine(true);
        editor.setText(value);
        editor.setHint(hint);
        editor.setInputType(InputType.TYPE_CLASS_NUMBER);
        editor.setTextSize(14);
        return editor;
    }

    private void buildInterface() {
        ScrollView page = new ScrollView(this);
        page.setFillViewport(true);
        page.setBackgroundColor(Color.rgb(245, 247, 251));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(14), dp(12), dp(14), dp(18));
        page.addView(content);

        TextView title = text("MASL Studio", 25, Color.rgb(20, 32, 52));
        title.setTypeface(Typeface.DEFAULT_BOLD);
        content.addView(title);

        statusView = text("Starting native MASL core…", 12,
                Color.rgb(75, 88, 108));
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        statusParams.bottomMargin = dp(10);
        content.addView(statusView, statusParams);

        TextView sourceLabel = text("MASL source", 15,
                Color.rgb(25, 41, 66));
        sourceLabel.setTypeface(Typeface.DEFAULT_BOLD);
        content.addView(sourceLabel);

        sourceEditor = new EditText(this);
        sourceEditor.setGravity(Gravity.TOP | Gravity.START);
        sourceEditor.setTypeface(Typeface.MONOSPACE);
        sourceEditor.setTextSize(13);
        sourceEditor.setTextColor(Color.rgb(27, 37, 53));
        sourceEditor.setHint("Write MASL source here…");
        sourceEditor.setInputType(InputType.TYPE_CLASS_TEXT |
                InputType.TYPE_TEXT_FLAG_MULTI_LINE |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        sourceEditor.setSingleLine(false);
        sourceEditor.setMinLines(15);
        sourceEditor.setMaxLines(28);
        sourceEditor.setPadding(dp(10), dp(10), dp(10), dp(10));
        sourceEditor.setBackgroundColor(Color.WHITE);
        LinearLayout.LayoutParams editorParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(330));
        editorParams.topMargin = dp(6);
        editorParams.bottomMargin = dp(8);
        content.addView(sourceEditor, editorParams);

        addRow(content,
                button("Example", this::loadExample),
                button("Save", this::saveSource),
                button("Load", this::loadSource));

        addRow(content,
                button("Lex", () -> analyze("lex")),
                button("Parse", () -> analyze("parse")),
                button("Check", () -> analyze("check")));

        addRow(content,
                button("Typed IR", () -> analyze("ir")),
                button("Build MASB", this::compileSource),
                button("Run", this::runProgram));

        TextView runLabel = text("Run a compiled function", 15,
                Color.rgb(25, 41, 66));
        runLabel.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams runLabelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        runLabelParams.topMargin = dp(8);
        content.addView(runLabel, runLabelParams);

        functionEditor = new EditText(this);
        functionEditor.setSingleLine(true);
        functionEditor.setText("answer");
        functionEditor.setHint("Function name");
        functionEditor.setTextSize(14);
        content.addView(functionEditor);

        argumentsEditor = new EditText(this);
        argumentsEditor.setSingleLine(true);
        argumentsEditor.setText("");
        argumentsEditor.setHint("Arguments, comma separated (e.g. 21)");
        argumentsEditor.setTextSize(14);
        content.addView(argumentsEditor);

        TextView outputLabel = text("Output", 15, Color.rgb(25, 41, 66));
        outputLabel.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams outputLabelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        outputLabelParams.topMargin = dp(12);
        content.addView(outputLabel, outputLabelParams);

        resultView = text("Load the example, then press Check or Build MASB.",
                12, Color.rgb(223, 231, 243));
        resultView.setTypeface(Typeface.MONOSPACE);
        resultView.setTextIsSelectable(true);
        resultView.setPadding(dp(10), dp(10), dp(10), dp(10));
        resultView.setBackgroundColor(Color.rgb(20, 29, 44));
        LinearLayout.LayoutParams resultParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(210));
        resultParams.topMargin = dp(6);
        content.addView(resultView, resultParams);

        TextView simulatorLabel = text("CPU / GPU architecture simulator", 17,
                Color.rgb(25, 41, 66));
        simulatorLabel.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams simulatorLabelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        simulatorLabelParams.topMargin = dp(14);
        content.addView(simulatorLabel, simulatorLabelParams);

        cpuInstructionEditor = numberEditor("50000", "CPU dynamic instructions");
        content.addView(cpuInstructionEditor);
        addRow(content, button("Simulate CPU", this::simulateCpu));

        gpuInstructionEditor = numberEditor("64", "GPU instructions per thread");
        content.addView(gpuInstructionEditor);
        gpuThreadEditor = numberEditor("32768", "GPU thread count");
        content.addView(gpuThreadEditor);
        TextView patternLabel = text("GPU memory access pattern", 13,
                Color.rgb(75, 88, 108));
        content.addView(patternLabel);
        memoryPatternSpinner = new Spinner(this);
        String[] patterns = {"coalesced", "strided", "random"};
        ArrayAdapter<String> patternAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, patterns);
        patternAdapter.setDropDownViewResource(
                android.R.layout.simple_spinner_dropdown_item);
        memoryPatternSpinner.setAdapter(patternAdapter);
        content.addView(memoryPatternSpinner);
        addRow(content, button("Simulate GPU", this::simulateGpu));

        TextView simulatorHelp = text(
                "The simulator runs deterministic generated workloads. Change workload sizes and " +
                "the GPU access pattern, then run CPU or GPU. Reports include cycles, IPC, " +
                "caches, DRAM, occupancy, warp lanes, bank conflicts and transfer costs.",
                11, Color.rgb(89, 101, 120));
        simulatorHelp.setPadding(dp(2), dp(6), dp(2), dp(6));
        content.addView(simulatorHelp);

        TextView note = text(
                "MASL source editing, analysis, MASB compilation and scalar VM execution are " +
                "available. CPU/GPU simulation is exposed as a built-in workload API; " +
                "hardware declarations in .masl are not yet lowered automatically into this engine.",
                11, Color.rgb(89, 101, 120));
        note.setPadding(dp(2), dp(10), dp(2), 0);
        content.addView(note);

        setContentView(page);
        if (NATIVE_AVAILABLE) {
            statusView.setText("Native core " + nativeVersion() +
                    " • Android ARM64 • local project storage");
        } else {
            statusView.setText("Native core unavailable: " + NATIVE_LOAD_ERROR);
            appendOutput("FATAL: MASL native runtime could not be loaded.\n" +
                    NATIVE_LOAD_ERROR + "\n" +
                    "The editor and local project storage remain available.");
        }
    }

    private void loadInitialSource() {
        File saved = new File(getFilesDir(), SOURCE_FILE);
        if (saved.isFile()) {
            loadSource();
        } else {
            loadExample();
        }
    }

    private String sourceText() {
        return sourceEditor.getText().toString();
    }

    private void hideKeyboard() {
        View focused = getCurrentFocus();
        if (focused != null) {
            InputMethodManager manager = (InputMethodManager)
                    getSystemService(Context.INPUT_METHOD_SERVICE);
            if (manager != null) {
                manager.hideSoftInputFromWindow(focused.getWindowToken(), 0);
            }
            focused.clearFocus();
        }
    }

    private void setBusy(boolean busy) {
        for (Button button : actionButtons) {
            button.setEnabled(!busy);
        }
        statusView.setText(busy ? "Working in the MASL native core…" :
                "Ready • files remain in this app's private storage");
    }

    private void runTask(String label, java.util.concurrent.Callable<String> task) {
        hideKeyboard();
        setBusy(true);
        worker.execute(() -> {
            String output;
            try {
                output = task.call();
            } catch (Throwable error) {
                output = label + " failed: " + error.getClass().getSimpleName() +
                        ": " + error.getMessage();
            }
            final String finalOutput = output;
            runOnUiThread(() -> {
                appendOutput(label + "\n" + finalOutput);
                setBusy(false);
            });
        });
    }

    private void appendOutput(String value) {
        resultView.setText(value == null || value.isEmpty() ? "(no output)" : value);
    }

    private void loadExample() {
        sourceEditor.setText(SAMPLE);
        functionEditor.setText("answer");
        argumentsEditor.setText("");
        appendOutput("Loaded the scalar-function MASL example.\n" +
                "Try Check → Build MASB → Run (answer).");
    }

    private void saveSource() {
        final String source = sourceText();
        runTask("Save project", () -> {
            File file = new File(getFilesDir(), SOURCE_FILE);
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(source.getBytes(StandardCharsets.UTF_8));
            }
            return "Saved " + file.getName() + " (" + file.length() +
                    " bytes) in private app storage.";
        });
    }

    private void loadSource() {
        runTask("Load project", () -> {
            File file = new File(getFilesDir(), SOURCE_FILE);
            if (!file.isFile()) {
                return "No saved source file exists yet. Press Example or Save.";
            }
            if (file.length() > 2_000_000L) {
                return "Saved source exceeds the 2 MB editor safety limit.";
            }
            byte[] bytes = new byte[(int) file.length()];
            try (FileInputStream input = new FileInputStream(file)) {
                int offset = 0;
                while (offset < bytes.length) {
                    int count = input.read(bytes, offset, bytes.length - offset);
                    if (count <= 0) break;
                    offset += count;
                }
                final String loaded = new String(bytes, 0, offset,
                        StandardCharsets.UTF_8);
                runOnUiThread(() -> sourceEditor.setText(loaded));
                return "Loaded " + file.getName() + " (" + offset + " bytes).";
            }
        });
    }

    private void analyze(String action) {
        final String source = sourceText();
        runTask("MASL " + action, () -> nativeAnalyze(source, action));
    }

    private void compileSource() {
        final String source = sourceText();
        runTask("Build MASB", () -> {
            byte[] bytes = nativeCompile(source);
            File file = new File(getFilesDir(), BYTECODE_FILE);
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(bytes);
            }
            return "MASB compiled and saved.\n" + bytes.length +
                    " bytes\nFunctions are executable through the Run panel.\n" +
                    "Private file: " + file.getName();
        });
    }

    private void runProgram() {
        final String function = functionEditor.getText().toString().trim();
        final String rawArguments = argumentsEditor.getText().toString().trim();
        final String[] arguments;
        if (rawArguments.isEmpty()) {
            arguments = new String[0];
        } else {
            String[] parts = rawArguments.split(",", -1);
            arguments = new String[parts.length];
            for (int index = 0; index < parts.length; index++) {
                arguments[index] = parts[index].trim();
            }
        }
        runTask("Run " + function, () -> {
            File file = new File(getFilesDir(), BYTECODE_FILE);
            if (!file.isFile()) {
                return "No MASB file found. Press Build MASB first.";
            }
            if (file.length() <= 0 || file.length() > 64L * 1024L * 1024L) {
                return "MASB file is empty or exceeds 64 MiB.";
            }
            byte[] bytes = new byte[(int) file.length()];
            try (FileInputStream input = new FileInputStream(file)) {
                int offset = 0;
                while (offset < bytes.length) {
                    int count = input.read(bytes, offset, bytes.length - offset);
                    if (count <= 0) break;
                    offset += count;
                }
                if (offset != bytes.length) {
                    return "Could not read the complete MASB file.";
                }
            }
            return nativeRun(bytes, function, arguments);
        });
    }

    private void simulateCpu() {
        final long instructionCount;
        try {
            instructionCount = Long.parseLong(cpuInstructionEditor.getText().toString().trim());
        } catch (NumberFormatException error) {
            appendOutput("CPU instruction count must be a positive integer.");
            return;
        }
        if (instructionCount <= 0L || instructionCount > 4_000_000L) {
            appendOutput("CPU instruction count must be in the range 1..4000000 (Android safety limit).");
            return;
        }
        final String memoryPattern = memoryPatternSpinner.getSelectedItem().toString();
        runTask("CPU architecture simulation", () -> nativeSimulate(
                "cpu", instructionCount, 0L, 20261123L, memoryPattern));
    }

    private void simulateGpu() {
        final long instructionsPerThread;
        final long threadCount;
        try {
            instructionsPerThread = Long.parseLong(
                    gpuInstructionEditor.getText().toString().trim());
            threadCount = Long.parseLong(gpuThreadEditor.getText().toString().trim());
        } catch (NumberFormatException error) {
            appendOutput("GPU instructions per thread and thread count must be positive integers.");
            return;
        }
        if (instructionsPerThread <= 0L || instructionsPerThread > 100_000L ||
                threadCount <= 0L || threadCount > 4_000_000L) {
            appendOutput("GPU limits: instructions per thread 1..100000; threads 1..4000000 (Android safety limit).");
            return;
        }
        final String memoryPattern = memoryPatternSpinner.getSelectedItem().toString();
        runTask("GPU architecture simulation", () -> nativeSimulate(
                "gpu", instructionsPerThread, threadCount, 20261123L, memoryPattern));
    }

    @Override
    protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }
}
