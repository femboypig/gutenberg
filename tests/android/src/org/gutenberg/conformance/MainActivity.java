package org.gutenberg.conformance;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.content.SharedPreferences;
import android.view.View;
import android.widget.EditText;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;

public final class MainActivity extends Activity {
    private int count;
    private TextView status;
    private EditText input;
    private SharedPreferences preferences;

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        try {
            BufferedReader reader = new BufferedReader(new InputStreamReader(getAssets().open("message.txt")));
            if (!"Gutenberg 😀".equals(reader.readLine())) throw new IllegalStateException("asset decoding");
            reader.close();
            FileOutputStream output = openFileOutput("checkpoint.bin", MODE_PRIVATE);
            output.write(127);
            output.close();
            FileInputStream inputFile = openFileInput("checkpoint.bin");
            if (inputFile.read() != 127) throw new IllegalStateException("file persistence");
            inputFile.close();
            if (new File(getFilesDir(), "checkpoint.bin").length() != 1) throw new IllegalStateException("file metadata");
        } catch (IOException failure) {
            throw new RuntimeException(failure);
        }
        setContentView(R.layout.main);
        preferences = getSharedPreferences("counter", MODE_PRIVATE);
        count = preferences.getInt("value", 0);
        status = (TextView) findViewById(R.id.status);
        input = (EditText) findViewById(R.id.input);
        status.setText("Count: " + count);
        findViewById(R.id.increment).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                count++;
                preferences.edit().putInt("value", count).apply();
                status.setText(input.getText().toString() + ": " + count);
            }
        });
        new Handler().post(new Runnable() {
            @Override public void run() { setTitle("Handler ready"); }
        });
    }
}
