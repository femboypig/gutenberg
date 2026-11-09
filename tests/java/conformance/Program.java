package conformance;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.BufferedReader;
import java.io.StringReader;

public final class Program {
    private static final int seed = initialize();
    private static int initialize() { return 7; }

    interface Operation { int apply(int value); }
    interface DefaultRoot { default int defaultValue() { return 5; } }
    interface DefaultLeft extends DefaultRoot {}
    interface DefaultRight extends DefaultRoot {}
    interface DefaultSpecific extends DefaultRoot { @Override default int defaultValue() { return 7; } }
    static final class DefaultDiamond implements DefaultLeft, DefaultRight {}
    static final class DefaultChild implements DefaultSpecific {}
    static final class DefaultSuper implements DefaultLeft {
        int callSuper() { return DefaultLeft.super.defaultValue(); }
    }
    static class DefaultParent { public int defaultValue() { return 11; } }
    static final class DefaultOverride extends DefaultParent implements DefaultSpecific {}
    static class Parent { int value; Parent(int value) { this.value = value; } int read() { return value; } }
    static final class Child extends Parent implements Operation {
        Child(int value) { super(value); }
        @Override int read() { return super.read() + 1; }
        @Override public int apply(int value) { return read() * value; }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new IllegalStateException(message);
    }
    private static int packed(int n) {
        switch (n) { case 1: return 10; case 2: return 20; case 3: return 30; default: return -1; }
    }
    private static int sparse(int n) {
        switch (n) { case -100: return 2; case 500: return 3; case 90000: return 5; default: return 0; }
    }
    private static int divide(int n, int divisor) {
        try { return n / divisor; } catch (ArithmeticException expected) { return 17; }
    }
    private static long wide(long n, long amount) { return (n + amount) ^ 0x1234567890abcdefL; }

    public static int run() {
        int sum = 0;
        for (int i = 1; i <= 100; i++) sum += i;
        require(sum == 5050, "loop");
        require(seed == 7, "class initialization");
        require(packed(2) == 20 && packed(9) == -1, "packed switch");
        require(sparse(90000) == 5 && sparse(-100) == 2, "sparse switch");
        require(divide(1, 0) == 17, "catch handler");
        require(wide(Long.MAX_VALUE, 1) == (Long.MIN_VALUE ^ 0x1234567890abcdefL), "wide arithmetic");
        int[] values = {2, 3, 5, 7, 11};
        values[2] *= 4;
        require(values.length == 5 && values[2] == 20, "array payload");
        int[] copy = new int[5];
        System.arraycopy(values, 0, copy, 0, 5);
        require(copy[4] == 11, "arraycopy");
        int[][] grid = (int[][]) java.lang.reflect.Array.newInstance(Integer.TYPE, new int[]{3, 4});
        grid[1][2] = 17;
        require(grid.length == 3 && grid[0].length == 4 && grid[1][2] == 17, "reflective multidimensional arrays");
        require(java.lang.reflect.Array.getLength(grid) == 3 && grid.getClass().getComponentType() == int[].class,
                "array class metadata");
        require(int.class.isPrimitive() && "int".equals(int.class.getName()), "primitive class metadata");
        Parent parent = new Child(6);
        require(parent.read() == 7, "virtual dispatch");
        require(((Operation) parent).apply(3) == 21, "interface dispatch");
        require(parent instanceof Operation, "instanceof");
        require(new DefaultDiamond().defaultValue() == 5, "diamond interface default");
        require(new DefaultChild().defaultValue() == 7, "most specific interface default");
        require(new DefaultOverride().defaultValue() == 11, "class overrides interface default");
        require(new DefaultSuper().callSuper() == 5, "inherited interface super call");
        require(Math.abs(-9) == 9 && Math.max(3, 8) == 8, "math");
        require((int) Math.sqrt(81.0) == 9, "double conversion");
        String text = "value=" + sum + ":" + true;
        require(text.equals("value=5050:true"), "string builder");
        require(text.substring(6, 10).equals("5050"), "substring");
        require("мир 😀".length() == 6, "unicode");
        require(Integer.valueOf(42).intValue() == 42, "boxing");
        ThreadLocal<String> local = new ThreadLocal<String>() {
            @Override protected String initialValue() { return "initial"; }
        };
        require("initial".equals(local.get()), "thread local initial value");
        local.set(null);
        require(local.get() == null, "thread local null value");
        local.remove();
        require("initial".equals(local.get()), "thread local remove");
        List<String> list = new ArrayList<String>();
        list.add("one"); list.add("two"); list.add(1, "middle");
        require(list.get(1).equals("middle") && list.size() == 3, "list operations");
        int length = 0;
        for (String value : list) length += value.length();
        require(length == 12, "iterator");
        require(list.equals(Arrays.asList("one", "middle", "two")), "list equality");
        Map<String, Integer> map = new HashMap<String, Integer>();
        map.put("answer", Integer.valueOf(42));
        require(map.get(new String("answer")).intValue() == 42, "map key equality");
        Map.Entry<String, Integer> entry = map.entrySet().iterator().next();
        entry.setValue(Integer.valueOf(7));
        require(map.get("answer").intValue() == 7 && map.keySet().contains("answer"), "backed map views");
        map.computeIfAbsent("computed", new java.util.function.Function<String, Integer>() {
            @Override public Integer apply(String key) { return Integer.valueOf(key.length()); }
        });
        require(map.get("computed").intValue() == 8, "map computation");
        java.util.Iterator<String> keys = map.keySet().iterator();
        keys.next(); keys.remove();
        require(map.size() == 1, "map iterator removal");
        java.util.Hashtable<String, String> table = new java.util.Hashtable<String, String>();
        table.put("key", "value");
        require(table.elements().nextElement().equals("value"), "hashtable enumeration");
        try { table.put("null", null); throw new IllegalStateException("hashtable accepted null"); }
        catch (NullPointerException expected) {}
        Map<String, String> cache = new java.util.LinkedHashMap<String, String>(4, 0.75f, true) {
            @Override protected boolean removeEldestEntry(Map.Entry<String, String> eldest) { return size() > 2; }
        };
        cache.put("a", "a"); cache.put("b", "b"); cache.get("a"); cache.put("c", "c");
        require(cache.containsKey("a") && !cache.containsKey("b"), "linked map eviction");
        require(Integer.parseInt("-123") == -123, "integer parsing");
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        output.write(255); output.write(0); output.write(42);
        ByteArrayInputStream input = new ByteArrayInputStream(output.toByteArray());
        require(input.read() == 255 && input.read() == 0 && input.read() == 42 && input.read() == -1, "byte streams");
        try {
            BufferedReader reader = new BufferedReader(new StringReader("one\r\nмир 😀\n"));
            require(reader.readLine().equals("one") && reader.readLine().equals("мир 😀") && reader.readLine() == null, "character streams");
            reader.close();
        } catch (java.io.IOException failure) { throw new IllegalStateException(failure); }
        require("\ud800".charAt(0) == '\ud800', "unpaired surrogate constant");
        System.out.println("conformance passed");
        return 42;
    }

    public static void main(String[] args) { System.out.println("result: " + run()); }
}
