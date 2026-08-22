; ModuleID = 'tests/edge_cases/05_switch.c'
source_filename = "tests/edge_cases/05_switch.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @dense_switch(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  switch i32 %4, label %15 [
    i32 0, label %5
    i32 1, label %6
    i32 2, label %7
    i32 3, label %8
    i32 4, label %9
    i32 5, label %10
    i32 6, label %11
    i32 7, label %12
    i32 8, label %13
    i32 9, label %14
  ]

5:                                                ; preds = %1
  store i32 10, ptr %2, align 4
  br label %16

6:                                                ; preds = %1
  store i32 20, ptr %2, align 4
  br label %16

7:                                                ; preds = %1
  store i32 30, ptr %2, align 4
  br label %16

8:                                                ; preds = %1
  store i32 40, ptr %2, align 4
  br label %16

9:                                                ; preds = %1
  store i32 50, ptr %2, align 4
  br label %16

10:                                               ; preds = %1
  store i32 60, ptr %2, align 4
  br label %16

11:                                               ; preds = %1
  store i32 70, ptr %2, align 4
  br label %16

12:                                               ; preds = %1
  store i32 80, ptr %2, align 4
  br label %16

13:                                               ; preds = %1
  store i32 90, ptr %2, align 4
  br label %16

14:                                               ; preds = %1
  store i32 100, ptr %2, align 4
  br label %16

15:                                               ; preds = %1
  store i32 -1, ptr %2, align 4
  br label %16

16:                                               ; preds = %15, %14, %13, %12, %11, %10, %9, %8, %7, %6, %5
  %17 = load i32, ptr %2, align 4
  ret i32 %17
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @sparse_switch(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  switch i32 %4, label %10 [
    i32 1, label %5
    i32 100, label %6
    i32 999, label %7
    i32 5000, label %8
    i32 9999, label %9
  ]

5:                                                ; preds = %1
  store i32 100, ptr %2, align 4
  br label %11

6:                                                ; preds = %1
  store i32 200, ptr %2, align 4
  br label %11

7:                                                ; preds = %1
  store i32 300, ptr %2, align 4
  br label %11

8:                                                ; preds = %1
  store i32 400, ptr %2, align 4
  br label %11

9:                                                ; preds = %1
  store i32 500, ptr %2, align 4
  br label %11

10:                                               ; preds = %1
  store i32 0, ptr %2, align 4
  br label %11

11:                                               ; preds = %10, %9, %8, %7, %6, %5
  %12 = load i32, ptr %2, align 4
  ret i32 %12
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @fallthrough_switch(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  %4 = load i32, ptr %2, align 4
  switch i32 %4, label %17 [
    i32 1, label %5
    i32 2, label %8
    i32 3, label %11
    i32 4, label %14
  ]

5:                                                ; preds = %1
  %6 = load i32, ptr %3, align 4
  %7 = add nsw i32 %6, 10
  store i32 %7, ptr %3, align 4
  br label %8

8:                                                ; preds = %1, %5
  %9 = load i32, ptr %3, align 4
  %10 = add nsw i32 %9, 20
  store i32 %10, ptr %3, align 4
  br label %11

11:                                               ; preds = %1, %8
  %12 = load i32, ptr %3, align 4
  %13 = add nsw i32 %12, 30
  store i32 %13, ptr %3, align 4
  br label %18

14:                                               ; preds = %1
  %15 = load i32, ptr %3, align 4
  %16 = add nsw i32 %15, 40
  store i32 %16, ptr %3, align 4
  br label %18

17:                                               ; preds = %1
  store i32 -1, ptr %3, align 4
  br label %18

18:                                               ; preds = %17, %14, %11
  %19 = load i32, ptr %3, align 4
  ret i32 %19
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @nested_switch(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  store i32 0, ptr %5, align 4
  %6 = load i32, ptr %4, align 4
  switch i32 %6, label %19 [
    i32 1, label %7
    i32 2, label %13
  ]

7:                                                ; preds = %2
  %8 = load i32, ptr %3, align 4
  switch i32 %8, label %11 [
    i32 1, label %9
    i32 2, label %10
  ]

9:                                                ; preds = %7
  store i32 11, ptr %5, align 4
  br label %12

10:                                               ; preds = %7
  store i32 12, ptr %5, align 4
  br label %12

11:                                               ; preds = %7
  store i32 10, ptr %5, align 4
  br label %12

12:                                               ; preds = %11, %10, %9
  br label %20

13:                                               ; preds = %2
  %14 = load i32, ptr %3, align 4
  switch i32 %14, label %17 [
    i32 1, label %15
    i32 2, label %16
  ]

15:                                               ; preds = %13
  store i32 21, ptr %5, align 4
  br label %18

16:                                               ; preds = %13
  store i32 22, ptr %5, align 4
  br label %18

17:                                               ; preds = %13
  store i32 20, ptr %5, align 4
  br label %18

18:                                               ; preds = %17, %16, %15
  br label %20

19:                                               ; preds = %2
  store i32 0, ptr %5, align 4
  br label %20

20:                                               ; preds = %19, %18, %12
  %21 = load i32, ptr %5, align 4
  ret i32 %21
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @expr_switch(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  store i32 5, ptr %4, align 4
  store i32 10, ptr %5, align 4
  %6 = load i32, ptr %3, align 4
  switch i32 %6, label %25 [
    i32 5, label %7
    i32 10, label %10
    i32 15, label %13
    i32 20, label %17
    i32 25, label %21
  ]

7:                                                ; preds = %1
  %8 = load i32, ptr %4, align 4
  %9 = mul nsw i32 %8, 2
  store i32 %9, ptr %2, align 4
  br label %26

10:                                               ; preds = %1
  %11 = load i32, ptr %5, align 4
  %12 = mul nsw i32 %11, 3
  store i32 %12, ptr %2, align 4
  br label %26

13:                                               ; preds = %1
  %14 = load i32, ptr %4, align 4
  %15 = load i32, ptr %5, align 4
  %16 = add nsw i32 %14, %15
  store i32 %16, ptr %2, align 4
  br label %26

17:                                               ; preds = %1
  %18 = load i32, ptr %4, align 4
  %19 = load i32, ptr %5, align 4
  %20 = mul nsw i32 %18, %19
  store i32 %20, ptr %2, align 4
  br label %26

21:                                               ; preds = %1
  %22 = load i32, ptr %4, align 4
  %23 = load i32, ptr %5, align 4
  %24 = sub nsw i32 %22, %23
  store i32 %24, ptr %2, align 4
  br label %26

25:                                               ; preds = %1
  store i32 0, ptr %2, align 4
  br label %26

26:                                               ; preds = %25, %21, %17, %13, %10, %7
  %27 = load i32, ptr %2, align 4
  ret i32 %27
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @large_switch(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  switch i32 %4, label %25 [
    i32 1, label %5
    i32 2, label %6
    i32 3, label %7
    i32 4, label %8
    i32 5, label %9
    i32 6, label %10
    i32 7, label %11
    i32 8, label %12
    i32 9, label %13
    i32 10, label %14
    i32 11, label %15
    i32 12, label %16
    i32 13, label %17
    i32 14, label %18
    i32 15, label %19
    i32 16, label %20
    i32 17, label %21
    i32 18, label %22
    i32 19, label %23
    i32 20, label %24
  ]

5:                                                ; preds = %1
  store i32 1, ptr %2, align 4
  br label %26

6:                                                ; preds = %1
  store i32 4, ptr %2, align 4
  br label %26

7:                                                ; preds = %1
  store i32 9, ptr %2, align 4
  br label %26

8:                                                ; preds = %1
  store i32 16, ptr %2, align 4
  br label %26

9:                                                ; preds = %1
  store i32 25, ptr %2, align 4
  br label %26

10:                                               ; preds = %1
  store i32 36, ptr %2, align 4
  br label %26

11:                                               ; preds = %1
  store i32 49, ptr %2, align 4
  br label %26

12:                                               ; preds = %1
  store i32 64, ptr %2, align 4
  br label %26

13:                                               ; preds = %1
  store i32 81, ptr %2, align 4
  br label %26

14:                                               ; preds = %1
  store i32 100, ptr %2, align 4
  br label %26

15:                                               ; preds = %1
  store i32 121, ptr %2, align 4
  br label %26

16:                                               ; preds = %1
  store i32 144, ptr %2, align 4
  br label %26

17:                                               ; preds = %1
  store i32 169, ptr %2, align 4
  br label %26

18:                                               ; preds = %1
  store i32 196, ptr %2, align 4
  br label %26

19:                                               ; preds = %1
  store i32 225, ptr %2, align 4
  br label %26

20:                                               ; preds = %1
  store i32 256, ptr %2, align 4
  br label %26

21:                                               ; preds = %1
  store i32 289, ptr %2, align 4
  br label %26

22:                                               ; preds = %1
  store i32 324, ptr %2, align 4
  br label %26

23:                                               ; preds = %1
  store i32 361, ptr %2, align 4
  br label %26

24:                                               ; preds = %1
  store i32 400, ptr %2, align 4
  br label %26

25:                                               ; preds = %1
  store i32 -1, ptr %2, align 4
  br label %26

26:                                               ; preds = %25, %24, %23, %22, %21, %20, %19, %18, %17, %16, %15, %14, %13, %12, %11, %10, %9, %8, %7, %6, %5
  %27 = load i32, ptr %2, align 4
  ret i32 %27
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @switch_in_loop(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 0, ptr %4, align 4
  br label %5

5:                                                ; preds = %31, %1
  %6 = load i32, ptr %4, align 4
  %7 = load i32, ptr %2, align 4
  %8 = icmp slt i32 %6, %7
  br i1 %8, label %9, label %34

9:                                                ; preds = %5
  %10 = load i32, ptr %4, align 4
  %11 = srem i32 %10, 4
  switch i32 %11, label %30 [
    i32 0, label %12
    i32 1, label %16
    i32 2, label %20
    i32 3, label %25
  ]

12:                                               ; preds = %9
  %13 = load i32, ptr %4, align 4
  %14 = load i32, ptr %3, align 4
  %15 = add nsw i32 %14, %13
  store i32 %15, ptr %3, align 4
  br label %30

16:                                               ; preds = %9
  %17 = load i32, ptr %4, align 4
  %18 = load i32, ptr %3, align 4
  %19 = sub nsw i32 %18, %17
  store i32 %19, ptr %3, align 4
  br label %30

20:                                               ; preds = %9
  %21 = load i32, ptr %4, align 4
  %22 = mul nsw i32 %21, 2
  %23 = load i32, ptr %3, align 4
  %24 = add nsw i32 %23, %22
  store i32 %24, ptr %3, align 4
  br label %30

25:                                               ; preds = %9
  %26 = load i32, ptr %4, align 4
  %27 = mul nsw i32 %26, 2
  %28 = load i32, ptr %3, align 4
  %29 = sub nsw i32 %28, %27
  store i32 %29, ptr %3, align 4
  br label %30

30:                                               ; preds = %9, %25, %20, %16, %12
  br label %31

31:                                               ; preds = %30
  %32 = load i32, ptr %4, align 4
  %33 = add nsw i32 %32, 1
  store i32 %33, ptr %4, align 4
  br label %5, !llvm.loop !8

34:                                               ; preds = %5
  %35 = load i32, ptr %3, align 4
  ret i32 %35
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  %3 = call i32 @dense_switch(i32 noundef 5)
  %4 = load i32, ptr %2, align 4
  %5 = add nsw i32 %4, %3
  store i32 %5, ptr %2, align 4
  %6 = call i32 @sparse_switch(i32 noundef 999)
  %7 = load i32, ptr %2, align 4
  %8 = add nsw i32 %7, %6
  store i32 %8, ptr %2, align 4
  %9 = call i32 @fallthrough_switch(i32 noundef 2)
  %10 = load i32, ptr %2, align 4
  %11 = add nsw i32 %10, %9
  store i32 %11, ptr %2, align 4
  %12 = call i32 @nested_switch(i32 noundef 1, i32 noundef 2)
  %13 = load i32, ptr %2, align 4
  %14 = add nsw i32 %13, %12
  store i32 %14, ptr %2, align 4
  %15 = call i32 @expr_switch(i32 noundef 10)
  %16 = load i32, ptr %2, align 4
  %17 = add nsw i32 %16, %15
  store i32 %17, ptr %2, align 4
  %18 = call i32 @large_switch(i32 noundef 15)
  %19 = load i32, ptr %2, align 4
  %20 = add nsw i32 %19, %18
  store i32 %20, ptr %2, align 4
  %21 = call i32 @switch_in_loop(i32 noundef 100)
  %22 = load i32, ptr %2, align 4
  %23 = add nsw i32 %22, %21
  store i32 %23, ptr %2, align 4
  %24 = load i32, ptr %2, align 4
  ret i32 %24
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "tests/edge_cases\\05_switch.c", directory: "C:\\LLVM-full")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 2}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"uwtable", i32 2}
!6 = !{i32 1, !"MaxTLSAlign", i32 65536}
!7 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)"}
!8 = distinct !{!8, !9}
!9 = !{!"llvm.loop.mustprogress"}
