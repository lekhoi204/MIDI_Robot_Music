$fn = 64; // độ mịn bề mặt

difference() {
    // Khối trụ chính (cao 5mm, đường kính tùy bạn, ví dụ 10mm)
    cylinder(h = 5, d = 6.5, center = false);

    // Lỗ rỗng ở tâm (đường kính 3.6mm, sâu 2mm)
    translate([0, 0, 5 - 2])  // đặt lỗ bắt đầu từ mặt trên, ăn sâu xuống 2mm
        cylinder(h = 2, d = 3.6, center = false);
}
