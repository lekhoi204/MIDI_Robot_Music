// ==== Thông số ====
thickness = 5;   // độ dày theo trục Z
stem_height = 50; // chiều cao thân dọc
stem_width = 5;   // độ rộng thân dọc
arm_length =20;  // độ dài mỗi nhánh
arm_width = 4;    // độ rộng nhánh
angle_out = 60;   // góc xoay hai nhánh ra ngoài (độ)

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
        // Left flange
        translate([-10, 0, 0])
        cube([20, 4, 25], center = false);
       
    } 
   // Holes for M4 bolts (Ø4.5 mm)
    translate([0, 0, 15])
    rotate([90, 0, 0])
        cylinder(h =  10, r = 2.25, center = true, $fn = 60);
    
}
