// --- Tấm phẳng 18x30mm, dày 5mm, có 1 lỗ M4 ở tâm và 2 khớp tròn có lỗ xuyên tâm ---
thickness = 5;
length = 18;
width = 30;
hole_dia = 4.2;       // lỗ bắt vít M4 (cho ốc lọt)
pin_dia = 4;        // lỗ xuyên tâm của khớp (cho đinh/pin)

// --- Thông số khớp ---
knuckle_dia = 8;      // đường kính ngoài khớp
knuckle_len = 5;      // chiều dài mỗi khớp (theo trục Y)
gap = 8;    // khoảng cách giữa 2 khớp = chiều dài khớp

// --- Tấm phẳng có 1 lỗ M4 ở giữa ---
difference() {
    cube([length, width, thickness], center = false);

    // Lỗ M4 ở đúng tâm tấm
    translate([length / 2, width / 2, 0])
        cylinder(h = thickness + 0.1, d = hole_dia, $fn = 64);
}

// --- Hai khớp tròn sát mép, cách nhau đều ---
for (i = [0:1]) {
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
        translate([0, 0, -1])
            cylinder(h = knuckle_len + 2, d = pin_dia, $fn = 48);
    }
}
