// ==== Thông số ====
thickness = 10;   // độ dày theo trục Z
stem_height = 5; // chiều cao thân dọc
stem_width = 5;   // độ rộng thân dọc
arm_length = 18;  // độ dài mỗi nhánh
arm_width = 4;    // độ rộng nhánh
angle_out = 50;   // góc xoay hai nhánh ra ngoài (độ)
hole_d = 2.7;     // đường kính lỗ
hole_depth = 5;   // độ sâu lỗ

difference() {
    union() {
        // Thân dọc (giữa chữ Y)
        translate([-stem_width/2, 0, 0])
            cube([stem_width, stem_height, thickness]);

        // Nhánh trái
        translate([0, stem_height, 0])
            rotate([0, 0, angle_out])
                translate([-arm_width/2, 0, 0])
                    cube([arm_width, arm_length, thickness]);

        // Nhánh phải
        translate([0, stem_height, 0])
            rotate([0, 0, -angle_out])
                translate([-arm_width/2, 0, 0])
                    cube([arm_width, arm_length, thickness]);
    }

    // Lỗ ở giữa để gắn ốc
    translate([0, stem_height/3, thickness/2])
        rotate([90, 0, 0])
            cylinder(h = hole_depth, d = hole_d, center = true, $fn = 50);
}
