package org.gutenberg.conformance;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.StrictMode;
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
import java.net.HttpURLConnection;
import java.net.URL;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

public final class MainActivity extends Activity {
    private int count;
    private TextView status;
    private EditText input;
    private SharedPreferences preferences;

    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        try {
            JSONObject json = new JSONObject("{\"title\":\"Gutenberg\",\"items\":[1,null,{\"active\":true}]}");
            JSONArray items = json.getJSONArray("items");
            if (items != json.get("items") || !items.isNull(1)) throw new IllegalStateException("JSON identity");
            items.put(0, 42);
            json.put("large", Long.MAX_VALUE);
            json.put("nothing", JSONObject.NULL);
            JSONObject restored = new JSONObject(json.toString());
            if (restored.getLong("large") != Long.MAX_VALUE || restored.getJSONArray("items").getInt(0) != 42)
                throw new IllegalStateException("JSON serialization");
            if (!restored.getJSONArray("items").getJSONObject(2).getBoolean("active"))
                throw new IllegalStateException("JSON nesting");
            if (!restored.has("nothing") || restored.get("nothing") != JSONObject.NULL)
                throw new IllegalStateException("JSON null sentinel");
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
        } catch (IOException | JSONException failure) {
            throw new RuntimeException(failure);
        }
        setContentView(R.layout.main);
        StrictMode.setThreadPolicy(new StrictMode.ThreadPolicy.Builder().permitNetwork().build());
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
        findViewById(R.id.fetch).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                try {
                    HttpURLConnection connection = (HttpURLConnection) new URL("http://127.0.0.1:8765/message").openConnection();
                    connection.setConnectTimeout(3000);
                    connection.setReadTimeout(3000);
                    BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream()));
                    status.setText("HTTP " + connection.getResponseCode() + ": " + reader.readLine());
                    reader.close();
                    connection.disconnect();
                } catch (IOException failure) { status.setText("network error: " + failure.getMessage()); }
            }
        });
    }
}
