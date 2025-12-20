    // U Bracket - Corrected version for Tinkercad / OpenSCAD 
    // Dimensions in mm

    difference() {
        // Main solid U-shape
        union() {
            // tran
            translate([9, 0, 14.5])
                cube([136.9, 32, 2], center = false);
            // day trai
            cube([11, 32, 2], center = false);
            // day phai
            translate([145.9, 0, 0])
                cube([11, 32, 2], center = false);
            // Side walls connecting to middle
            //le trai 1
            translate([9, 0, 0])
                cube([2, 32, 14.5], center = false);
                //le trai 2
            translate([30.2, 0, 0])
                cube([2.7, 32, 14.5], center = false);
                    //le trai 3
            translate([52.1, 0, 0])
                cube([4.4, 32, 14.5], center = false);
                        //le trai 4
            translate([75.7, 0, 0])
                cube([7, 32, 14.5], center = false);
                            //le trai 5
            translate([118.9, 0, 0])
                cube([7.8, 32, 14.5], center = false);
            //mep trai duoi 1
            translate([11, 0, 0])
                cube([5, 1, 14.5], center = false);
                //mep trai duoi 2
            translate([32.9, 0, 0])
                cube([5, 1, 14.5], center = false);
                    //mep trai duoi 3
            translate([56.5, 0, 0])
                cube([5, 1, 14.5], center = false);
                        //mep trai duoi 4
            translate([82.7, 0, 0])
                cube([5, 1, 14.5], center = false);
                            //mep trai duoi 5
            translate([126.7, 0, 0])
                cube([5, 1, 14.5], center = false);
            //mep trai tren 1
            translate([11, 31, 0])
                cube([5, 1, 14.5], center = false);
                //mep trai tren 2
            translate([32.9, 31, 0])
                cube([5, 1, 14.5], center = false);
                    //mep trai tren 3
            translate([56.5, 31, 0])
                cube([5, 1, 14.5], center = false);
                        //mep trai tren 4
            translate([82.7, 31, 0])
                cube([5, 1, 14.5], center = false);
                            //mep trai tren 5
            translate([126.7, 31, 0])
                cube([5, 1, 14.5], center = false);
            //mep phai tren 1
            translate([24.2, 31, 0])
                cube([5, 1, 14.5], center = false);
                //mep phai tren 2
            translate([46.1, 31, 0])
                cube([5, 1, 14.5], center = false);
                    //mep phai tren 3
            translate([69.7, 31, 0])
                cube([5, 1, 14.5], center = false);
                        //mep phai tren 4
            translate([112.9, 31, 0])
                cube([5, 1, 14.5], center = false);
                            //mep phai tren 5
            translate([139.9, 31, 0])
                cube([5, 1, 14.5], center = false);
            //mep phai duoi 1
            translate([24.2, 0, 0])
                cube([5, 1, 14.5], center = false);
                //mep phai duoi 2
            translate([46.1, 0, 0])
                cube([5, 1, 14.5], center = false);
                    //mep phai duoi 3
            translate([69.7, 0, 0])
                cube([5, 1, 14.5], center = false);
                        //mep phai duoi 4
            translate([112.9, 0, 0])
                cube([5, 1, 14.5], center = false);
                            //mep phai duoi 5
            translate([139.9, 0, 0])
                cube([5, 1, 14.5], center = false);
            //le phai 1
            translate([28.2, 0, 0])
                cube([2, 32, 14.5], center = false);
                //le phai 2
            translate([50.1, 0, 0])
                cube([2, 32, 14.5], center = false);
                    //le phai 3
            translate([73.7, 0, 0])
                cube([2, 32, 14.5], center = false);
                        //le phai 4
            translate([116.9, 0, 0])
                cube([2, 32, 14.5], center = false);
                            //le phai 5
            translate([143.9, 0, 0])
                cube([2, 32, 14.5], center = false);
        }

        // Holes for M4 bolts (Ø4.5 mm)
        //lo trai
        translate([5, 16, 1])
            cylinder(h = 4, r = 2.25, center = true, $fn = 60);
        //lo phai
        translate([151.5, 16, 1])
            cylinder(h = 4, r = 2.25, center = true, $fn = 60);
    }