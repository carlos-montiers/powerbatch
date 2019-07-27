---
layout: default
title: Cursor
parent: Extensions
---

# Opacity
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## set @cursor
Set the console cursor size.

Parameters: Number between 0 and 100.
0 hides the cursor
1 shows the cursor
2-100 set that size.

```batch
set @cursor=50
```

## get @opacity
Get the console cursor size.

```batch
echo Cursor: !@cursor!
```

{: .fs-6 .fw-300 }