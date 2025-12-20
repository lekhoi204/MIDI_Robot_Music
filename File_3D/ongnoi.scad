// === Thông số chung ===
outer_d1 = 40.4;   // Đầu lớn của ống nón
outer_d2 = 27.8;   // Đầu nhỏ của ống nón
thickness = 4;     // Độ dày thành
length_cone = 20;  // Chiều dài ống nón
length_cyl = 20;   // Chiều dài ống trụ
inner_d1 = outer_d1 - 2*thickness;
inner_d2 = outer_d2 - 2*thickness;

// === Ống hình nón rỗng nằm ngang ===
module tapered_tube() {
    rotate([0,90,0])
    difference() {
        cylinder(h = length_cone, d1 = outer_d1, d2 = outer_d2, $fn = 100);
        translate([0,0,-0.01])
            cylinder(h = length_cone + 0.02, d1 = inner_d1, d2 = inner_d2, $fn = 100);
    }
}

// === Ống trụ rỗng nằm ngang ===
module round_tube() {
    rotate([0,90,0])
    difference() {
        cylinder(h = length_cyl, d = outer_d1, $fn = 100);
        cylinder(h = length_cyl + 0.02, d = inner_d1, $fn = 100);
    }
}
// === Ống trụ rỗng nằm ngang ===
module round_tube1() {
    translate([40,0,0])
    rotate([0,90,0])
    difference() {
        cylinder(h = 10, d = outer_d2, $fn = 100);
        cylinder(h = 10 + 0.02, d = inner_d2, $fn = 100);
    }
}

// === Nắp nửa hình tròn ở đầu nhỏ ===
module half_cap() {
    translate([37,0,0])  // Gắn ở đầu nhỏ
    rotate([0,90,0])
    intersection() {
        // Đĩa bịt
        difference() {
            cylinder(h = 3, d = outer_d2, $fn = 100);
        }
        // Giữ lại nửa trên
        translate([-outer_d2,-outer_d2,0])
            cube([outer_d2*2, outer_d2, thickness*2], center=false);
    }
}

// === Kết hợp toàn bộ ===

// 1. Ống trụ rỗng nằm ngang (phần đầu lớn)
round_tube();
round_tube1();
// 2. Ống nón nối tiếp theo sau
translate([length_cyl,0,0]) tapered_tube();

// 3. Nắp bịt nửa hình tròn ở đầu nhỏ
half_cap();
