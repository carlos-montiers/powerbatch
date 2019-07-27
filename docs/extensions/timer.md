---
layout: default
title: Timer
parent: Extensions
---

# Timer
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

Millisecond timer, with a resolution of 10-15ms.


## start the low-resolution timer
```batch
set @timer=start
```

## read the running timer
```batch
echo %@timer%
```

## stop the timer
```batch
set @timer=
set @timer=stop
```

## read the elapsed time
```batch
echo %@timer%
```

## Example
```batch
set @timer=start
echo Wait a little bit and press a key ...
pause > nul
set @timer=stop
echo Elapsed milliseconds: %@timer%
```

{: .fs-6 .fw-300 }