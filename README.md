# Reflection-Groups

The purpose of this project was to simulate reflections on basic geometry, and visualize it efficiently. While rendering geometry which has been reflected is relatively trivial, I wanted to create an application that could “recursively” reflect geometry across sets of mirrors. What this means is that a point could be rendered, then reflected across a mirror, then rendered again. This point could then be used in another subsequent reflection, and rendered again. The result of this is a kaleidoscopic effect where one piece of basic geometry can create large emergent patterns based only on its location, and the properties of the mirrors we use for reflection.

Even in a simple case where we take 1 point, and 3 mirrors, we can reflect that point in 3 different ways. A different reflection across each mirror. But now we can take these 3 new points and reflect each of them across each of the mirrors, resulting in 9 new points. As we recursively continue this process the number of points grows exponentially. While some points can be ignored as they are duplicates of previous ones, the number still grows rapidly and even in the simple case given, by the time we reach a reflection depth of around 20, we need to render over 25 million points. For this reason, if we want to be able to visualize any significant number of reflections, we need to be fairly efficient.

This project utilizes the GLFW library in order to make use of the OpenGL renderer. This allows the vast bulk of our computation to be moved to the GPU for rapid concurrent handling of our millions of point reflections.

In general, the program will take the user's cursor location as the input to the reflection simulation, and (depending on settings) will render and display the resulting group of possible reflections in real time. In the most basic form it will look like this: 

[BASIC POINT, LOW DEPTH, MIRRORS]

Here we draw one point at the user's cursor and use it as our input, and the solid white lines represent our three mirrors. Here the recursive depth has been set to 2, so the point is reflected once in each mirror, and then each of those points is reflected again in every mirror. Two reflection steps, so a “depth” of two.

[BASIC POINT, HIGH DEPTH]

Here is an example where our depth has been set substantially higher. This has a depth of DEPTH, and is computing POINTS points. Not all of these points are rendered as some fall too far outside our window, however each point’s location still needs to be computed in case it does. Note that the mirrors are still “there” for the purpose of reflections. Their renderer has simply been turned off so they are not visible.

Since rendering this many points can cause the image to become somewhat busy, there is an alternate shader set up to render out points as a density map instead. Here brighter areas represent a higher density of points in that region, and darker areas a lower density. While this does help see the emergent patterns from our reflections a bit better, the amount of time it takes to compute these density regions is a good amount higher than when just rendering individual points. For this reason the resolution of the window was lowered somewhat for these examples.

[DENSITY, B&W] [DENSITY, COLOUR]

There is one more type of rendering that this application supports and that is a trailing effect. What this does is adds a slow fading trail behind each of the rendered points. This is helpful for visualizing how movement of the initial point with the cursor causes movement in groups of reflected points.

[TRAILS, COLOUR]

Of course we can also just turn off frame clearing altogether and create some really interesting designs just by using our cursor like a pen.

[POINTS, RAINBOW, NO FRAME CLEAR]
