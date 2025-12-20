// U Bracket - Corrected version for Tinkercad / OpenSCAD 
// Dimensions in mm

difference() {
    // Main solid U-shape
    union() {
        // Middle raised section
        translate([9, 0, 14.5])
            cube([20, 28, 2], center = false);
        // Left flange
        cube([11, 28, 2], center = false);
        // Right flange
        translate([29, 0, 0])
            cube([11, 28, 2], center = false);
        // Side walls connecting to middle
        translate([9, 0, 2])
            cube([2, 28, 14.5], center = false);
        translate([9, 0, 0])
            cube([5, 0.5, 14.5], center = false);
        translate([9, 27.5, 0])
            cube([5, 0.5, 14.5], center = false);
        translate([26, 27.5, 0])
            cube([5, 0.5, 14.5], center = false);
        translate([26, 0, 0])
            cube([5, 0.5, 14.5], center = false);
        translate([29, 0, 2])
            cube([2, 28, 14.5], center = false);
    }

    // Holes for M4 bolts (Ø4.5 mm)
    translate([5, 13, 1])
        cylinder(h = 4, r = 2.25, center = true, $fn = 60);
    translate([35, 13, 1])
        cylinder(h = 4, r = 2.25, center = true, $fn = 60);
}