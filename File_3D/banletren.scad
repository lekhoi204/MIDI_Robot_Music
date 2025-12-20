// --- Tấm phẳng 30x20mm, dày 5mm, có 2 lỗ M3 và 3 khớp tròn có lỗ xuyên tâm ---
thickness = 5;
length = 30;
width = 20;
hole_dia = 4.2;       // lỗ bắt vít trên tấm
edge_margin = 8;      // khoảng cách lỗ đến mép theo chiều dài

// --- Thông số khớp ---
knuckle_dia = 8;      // đường kính ngoài khớp
knuckle_len = 5.5;      // chiều dài mỗi khớp (trục Y)
gap = 6.75;    // khoảng cách giữa 2 khớp = chiều dài khớp
pin_dia = 4;          // đường kính lỗ xuyên tâm để gắn đinh/pin

// --- Tấm phẳng có 2 lỗ M3 ---
difference() {
    cube([length, width, thickness], center = false);

    // Hai lỗ M3 ở giữa tấm
    for (x = [edge_margin, length - edge_margin]) {
        translate([x, width / 2, 0])
            cylinder(h = thickness + 0.1, d = hole_dia, $fn = 64);
    }
}

// --- Các khớp tròn và lỗ xuyên tâm ---
for (i = [0:2]) {
    // tạo khớp đặc trước
    translate([
        i * (knuckle_len + gap),
        -3,                   // dính sát mép tấm
        thickness / 2         // nằm giữa bề dày
    ])
    rotate([0, 90, 0])
    difference() {
        // khối tròn bên ngoài
        cylinder(h = knuckle_len, d = knuckle_dia, $fn = 64);

        // lỗ xuyên tâm để luồn đinh/pin
        translate([0, 0, -1])  // lỗ xuyên qua cả chiều dài khớp
            cylinder(h = knuckle_len + 2, d = pin_dia, $fn = 48);
    }
}
