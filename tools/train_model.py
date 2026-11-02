# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "torch>=2.0.0",
#     "torchvision>=0.15.0",
#     "pillow>=9.0.0",
# ]
# ///
"""Training, dataset preparation, and C++ weight export tool for the Basil Wilt Detection model."""

import argparse
import re
import shutil
import sys
from pathlib import Path


# Canopy Region-of-Interest (ROI) horizontal crop bounds [20% .. 75% of width].
# Removes the picture frame on the left (0..20%) and terracotta pot on the right (75..100%).
ROI_X_MIN = 0.20
ROI_X_MAX = 0.75


def parse_roi(roi_str: str) -> tuple[float, float]:
    """Parse 'x_min,x_max' fraction string (e.g. '0.20,0.75')."""
    x0_s, x1_s = roi_str.split(",", 1)
    return float(x0_s.strip()), float(x1_s.strip())


def crop_canopy_roi(img, x_min: float = ROI_X_MIN, x_max: float = ROI_X_MAX):
    """Crop a PIL image horizontally to the basil canopy ROI [x_min .. x_max]."""
    w, h = img.size
    # Guard against double-cropping an already-cropped image (aspect ratio < 1.0)
    if w <= h:
        return img
    x0 = int(round(w * x_min))
    x1 = int(round(w * x_max))
    return img.crop((x0, 0, x1, h))


def parse_ranges(range_str: str) -> set[int]:
    """Parse comma-separated numbers and hyphenated ranges (e.g. '1-45,120-179')."""
    indices: set[int] = set()
    parts = [p.strip() for p in range_str.split(",") if p.strip()]
    for part in parts:
        if "-" in part:
            start_s, end_s = part.split("-", 1)
            start = int(start_s)
            end = int(end_s)
            indices.update(range(start, end + 1))
        else:
            indices.add(int(part))
    return indices


def extract_index(filename: str) -> int | None:
    """Extract numeric index from filenames like 'img_00123.jpg'."""
    match = re.search(r"img_(\d+)", filename, re.IGNORECASE)
    if match:
        return int(match.group(1))
    return None


def cmd_prepare(args: argparse.Namespace) -> int:
    """Sort images into healthy/ and wilted/ folders by filename or index range."""
    src_dir = Path(args.source)
    dataset_dir = Path(args.out)
    healthy_dir = dataset_dir / "healthy"
    wilted_dir = dataset_dir / "wilted"

    healthy_dir.mkdir(parents=True, exist_ok=True)
    wilted_dir.mkdir(parents=True, exist_ok=True)

    if not src_dir.exists():
        print(f"Error: Source directory {src_dir} does not exist.", file=sys.stderr)
        return 1

    healthy_indices = parse_ranges(args.healthy) if args.healthy else set()
    wilted_indices = parse_ranges(args.wilted) if args.wilted else set()
    prefix = f"{args.prefix}_" if getattr(args, "prefix", None) else ""
    roi_x0, roi_x1 = parse_roi(args.roi_x)

    all_files = sorted(list(src_dir.glob("*.jpg")) + list(src_dir.glob("*.jpeg")))
    if not all_files:
        print(f"No JPEG images found in {src_dir}.", file=sys.stderr)
        return 1

    from PIL import Image, UnidentifiedImageError

    healthy_count = 0
    wilted_count = 0
    ignored_count = 0
    corrupted_count = 0

    for f in all_files:
        idx = extract_index(f.name)
        if idx is None:
            ignored_count += 1
            continue

        dest_name = f"{prefix}{f.name}"
        if idx in healthy_indices or idx in wilted_indices:
            try:
                with Image.open(f) as img:
                    rgb = img.convert("RGB")
                    cropped = crop_canopy_roi(rgb, roi_x0, roi_x1)
            except (UnidentifiedImageError, OSError):
                print(f"  [!] Skipping corrupted/invalid image: {f}")
                corrupted_count += 1
                continue

            if idx in healthy_indices:
                cropped.save(healthy_dir / dest_name, quality=95)
                healthy_count += 1
            elif idx in wilted_indices:
                cropped.save(wilted_dir / dest_name, quality=95)
                wilted_count += 1
        else:
            ignored_count += 1

    print(f"Dataset prepared at: {dataset_dir}")
    print(f"  • Healthy: {healthy_count} photos -> {healthy_dir}")
    print(f"  • Wilted:  {wilted_count} photos -> {wilted_dir}")
    print(f"  • Ignored / unassigned: {ignored_count} photos")
    if corrupted_count:
        print(f"  • Corrupted / skipped:  {corrupted_count} photos")
    return 0


def cmd_train(args: argparse.Namespace) -> int:
    """Train a transfer learning MobileNetV2 model locally."""
    import torch
    import torch.nn as nn
    from torch.utils.data import DataLoader, random_split
    from torchvision import datasets, models, transforms

    data_dir = Path(args.data)
    healthy_dir = data_dir / "healthy"
    wilted_dir = data_dir / "wilted"

    if not (healthy_dir.exists() and wilted_dir.exists()):
        print(f"Error: Expected folders 'healthy' and 'wilted' inside {data_dir}", file=sys.stderr)
        return 1

    healthy_count = len(list(healthy_dir.glob("*.jpg")))
    wilted_count = len(list(wilted_dir.glob("*.jpg")))
    print(f"Found {healthy_count} healthy images and {wilted_count} wilted images.")

    if healthy_count == 0 or wilted_count == 0:
        print("Error: Need at least 1 image in each class to train.", file=sys.stderr)
        return 1

    # Device selection (Apple Silicon MPS, CUDA, or CPU)
    if torch.backends.mps.is_available():
        device = torch.device("mps")
        print("Using Apple Silicon Metal (MPS) acceleration.")
    elif torch.cuda.is_available():
        device = torch.device("cuda")
        print("Using NVIDIA CUDA acceleration.")
    else:
        device = torch.device("cpu")
        print("Using CPU.")

    img_size = args.size
    train_transform = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomAffine(degrees=10, translate=(0.12, 0.08), scale=(0.90, 1.10)),
        transforms.ColorJitter(brightness=0.25, contrast=0.25, saturation=0.25, hue=0.06),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ])

    val_transform = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ])

    full_dataset = datasets.ImageFolder(str(data_dir))
    classes = full_dataset.classes
    print(f"Classes detected: {classes} (Index 0: {classes[0]}, Index 1: {classes[1]})")

    val_split = 0.2
    val_size = max(1, int(len(full_dataset) * val_split))
    train_size = len(full_dataset) - val_size

    torch.manual_seed(42)
    train_subset, val_subset = random_split(full_dataset, [train_size, val_size])

    class TransformSubset(torch.utils.data.Dataset):
        def __init__(self, subset, transform):
            self.subset = subset
            self.transform = transform

        def __getitem__(self, idx):
            x, y = self.subset[idx]
            return self.transform(x), y

        def __len__(self):
            return len(self.subset)

    train_dataset = TransformSubset(train_subset, train_transform)
    val_dataset = TransformSubset(val_subset, val_transform)

    train_loader = DataLoader(train_dataset, batch_size=args.batch, shuffle=True)
    val_loader = DataLoader(val_dataset, batch_size=args.batch, shuffle=False)

    print(f"Training set: {train_size} samples | Validation set: {val_size} samples")

    # Load MobileNetV2 pretrained backbone
    weights = models.MobileNet_V2_Weights.DEFAULT
    model = models.mobilenet_v2(weights=weights)

    # Freeze base feature extractor
    for param in model.features.parameters():
        param.requires_grad = False

    # Replace classifier head for binary output
    in_features = model.classifier[1].in_features
    model.classifier = nn.Sequential(
        nn.Dropout(p=0.2),
        nn.Linear(in_features, 2),
    )
    model.to(device)

    # Compute inverse-frequency class weights so class imbalance does not bias predictions
    class_counts = [0, 0]
    for _, y in full_dataset.samples:
        class_counts[y] += 1
    total_samples = sum(class_counts)
    class_weights = torch.tensor(
        [total_samples / (2.0 * max(1, c)) for c in class_counts],
        dtype=torch.float32,
        device=device,
    )
    criterion = nn.CrossEntropyLoss(weight=class_weights)
    optimizer = torch.optim.Adam(model.classifier.parameters(), lr=args.lr)

    epochs = args.epochs
    print(f"\nTraining for {epochs} epochs at input resolution {img_size}x{img_size}...")

    for epoch in range(1, epochs + 1):
        model.train()
        running_loss = 0.0
        correct = 0
        total = 0

        for inputs, labels in train_loader:
            inputs, labels = inputs.to(device), labels.to(device)
            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

            running_loss += loss.item() * inputs.size(0)
            _, preds = torch.max(outputs, 1)
            correct += (preds == labels).sum().item()
            total += labels.size(0)

        train_loss = running_loss / total
        train_acc = (correct / total) * 100.0

        # Evaluate on validation set
        model.eval()
        val_correct = 0
        val_total = 0
        tp = 0
        fp = 0
        fn = 0
        tn = 0

        with torch.no_grad():
            for inputs, labels in val_loader:
                inputs, labels = inputs.to(device), labels.to(device)
                outputs = model(inputs)
                _, preds = torch.max(outputs, 1)
                val_correct += (preds == labels).sum().item()
                val_total += labels.size(0)

                for p, l in zip(preds, labels):
                    if p == 1 and l == 1:
                        tp += 1
                    elif p == 1 and l == 0:
                        fp += 1
                    elif p == 0 and l == 1:
                        fn += 1
                    elif p == 0 and l == 0:
                        tn += 1

        val_acc = (val_correct / val_total) * 100.0
        print(f"Epoch {epoch:02d}/{epochs:02d} | Train Loss: {train_loss:.4f} | Train Acc: {train_acc:.1f}% | Val Acc: {val_acc:.1f}%")

    print("\n=== Final Validation Metrics ===")
    print(f"Accuracy: {val_acc:.1f}% ({val_correct}/{val_total})")
    print(f"Confusion Matrix [Class 0: {classes[0]}, Class 1: {classes[1]}]:")
    print(f"               Predicted {classes[0]}   Predicted {classes[1]}")
    print(f"Actual {classes[0]:<7}       {tn:^6d}          {fp:^6d}")
    print(f"Actual {classes[1]:<7}       {fn:^6d}          {tp:^6d}")

    out_model_path = Path(args.out)
    torch.save(model.state_dict(), out_model_path)
    print(f"\nModel weights saved to: {out_model_path.resolve()}")
    return 0


def cmd_export_cpp(args: argparse.Namespace) -> int:
    """Train a lightweight edge CNN on 32x32 RGB patches and export weights to a C++ header."""
    import torch
    import torch.nn as nn
    from torch.utils.data import DataLoader, random_split
    from torchvision import datasets, transforms

    data_dir = Path(args.data)
    healthy_dir = data_dir / "healthy"
    wilted_dir = data_dir / "wilted"

    if not (healthy_dir.exists() and wilted_dir.exists()):
        print(f"Error: Expected folders 'healthy' and 'wilted' inside {data_dir}", file=sys.stderr)
        return 1

    device = torch.device("mps" if torch.backends.mps.is_available() else "cpu")
    img_size = 32  # 32x32 RGB input (downsampled 3x from 96x96 on ESP32)

    train_transform = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomAffine(degrees=8, translate=(0.12, 0.08), scale=(0.90, 1.10)),
        transforms.ColorJitter(brightness=0.20, contrast=0.20, saturation=0.20, hue=0.05),
        transforms.ToTensor(),  # [0.0, 1.0] RGB
    ])

    val_transform = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.ToTensor(),
    ])

    full_dataset = datasets.ImageFolder(str(data_dir))
    classes = full_dataset.classes
    val_size = max(1, int(len(full_dataset) * 0.2))
    train_size = len(full_dataset) - val_size

    torch.manual_seed(42)
    train_subset, val_subset = random_split(full_dataset, [train_size, val_size])

    class TransformSubset(torch.utils.data.Dataset):
        def __init__(self, subset, transform):
            self.subset = subset
            self.transform = transform

        def __getitem__(self, idx):
            x, y = self.subset[idx]
            return self.transform(x), y

        def __len__(self):
            return len(self.subset)

    train_loader = DataLoader(TransformSubset(train_subset, train_transform), batch_size=16, shuffle=True)
    val_loader = DataLoader(TransformSubset(val_subset, val_transform), batch_size=16, shuffle=False)

    # Compact CNN: Conv(3->8, 3x3, pad=1) -> ReLU -> MaxPool(2) [16x16]
    #              -> Conv(8->16, 3x3, pad=1) -> ReLU -> MaxPool(2) [8x8]
    #              -> AdaptiveAvgPool(4x4) [16*4*4 = 256] -> Linear(256 -> 2)
    class TinyBasilCNN(nn.Module):
        def __init__(self):
            super().__init__()
            self.conv1 = nn.Conv2d(3, 8, kernel_size=3, padding=1)
            self.conv2 = nn.Conv2d(8, 16, kernel_size=3, padding=1)
            self.fc = nn.Linear(16 * 4 * 4, 2)

        def forward(self, x):
            x = torch.relu(self.conv1(x))
            x = torch.max_pool2d(x, 2)  # 32 -> 16
            x = torch.relu(self.conv2(x))
            x = torch.max_pool2d(x, 2)  # 16 -> 8
            x = nn.functional.avg_pool2d(x, 2)  # 8 -> 4
            x = x.view(x.size(0), -1)   # [B, 256]
            return self.fc(x)

    torch.manual_seed(42)
    model = TinyBasilCNN().to(device)
    class_counts = [0, 0]
    for _, y in full_dataset.samples:
        class_counts[y] += 1
    total_samples = sum(class_counts)
    class_weights = torch.tensor(
        [total_samples / (2.0 * max(1, c)) for c in class_counts],
        dtype=torch.float32,
        device=device,
    )
    criterion = nn.CrossEntropyLoss(weight=class_weights)
    optimizer = torch.optim.Adam(model.parameters(), lr=0.003, weight_decay=1e-4)

    epochs = args.epochs
    best_score = -1.0
    best_state = None
    best_val_acc = 0.0
    best_train_acc = 0.0

    print(f"Training TinyBasilCNN (32x32 RGB) for {epochs} epochs...")
    for epoch in range(1, epochs + 1):
        model.train()
        correct = 0
        total = 0
        running_loss = 0.0
        for inputs, labels in train_loader:
            inputs, labels = inputs.to(device), labels.to(device)
            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()
            running_loss += loss.item() * inputs.size(0)
            _, preds = torch.max(outputs, 1)
            correct += (preds == labels).sum().item()
            total += labels.size(0)

        train_acc = (correct / total) * 100.0

        model.eval()
        val_correct = 0
        val_total = 0
        val_loss = 0.0
        with torch.no_grad():
            for inputs, labels in val_loader:
                inputs, labels = inputs.to(device), labels.to(device)
                outputs = model(inputs)
                loss = criterion(outputs, labels)
                val_loss += loss.item() * inputs.size(0)
                _, preds = torch.max(outputs, 1)
                val_correct += (preds == labels).sum().item()
                val_total += labels.size(0)

        val_acc = (val_correct / val_total) * 100.0
        score = val_acc * 10.0 + train_acc - (val_loss / val_total)
        if score > best_score:
            best_score = score
            best_val_acc = val_acc
            best_train_acc = train_acc
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}

        if epoch % 5 == 0 or epoch == epochs:
            print(
                f"Epoch {epoch:02d}/{epochs:02d} | Loss: {running_loss/total:.4f} | "
                f"Train Acc: {train_acc:.1f}% | Val Acc: {val_acc:.1f}%"
            )

    if best_state is not None:
        model.load_state_dict(best_state)
    print(f"\nSelected best checkpoint -> Train Acc: {best_train_acc:.1f}% | Val Acc: {best_val_acc:.1f}%")
    model.cpu().eval()

    def fmt_array(name: str, tensor: torch.Tensor) -> str:
        flat = tensor.detach().numpy().flatten()
        vals = ", ".join(f"{v:.6f}f" for v in flat)
        return f"static const float {name}[{len(flat)}] = {{\n  {vals}\n}};\n"

    header_path = Path(args.header)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_content = (
        "// model_weights.h\n"
        "// Auto-generated by `uv run tools/train_model.py export-cpp`.\n"
        "// Architecture: TinyBasilCNN (32x32 RGB -> Conv3x3(8) -> MaxPool2 -> Conv3x3(16) -> MaxPool2 -> AvgPool2 -> Linear(256, 2))\n"
        f"// Classes: 0 = {classes[0]}, 1 = {classes[1]}\n\n"
        "#pragma once\n\n"
        "#define MODEL_INPUT_W 32\n"
        "#define MODEL_INPUT_H 32\n\n"
        + fmt_array("CONV1_WEIGHT", model.conv1.weight)
        + fmt_array("CONV1_BIAS", model.conv1.bias)
        + fmt_array("CONV2_WEIGHT", model.conv2.weight)
        + fmt_array("CONV2_BIAS", model.conv2.bias)
        + fmt_array("FC_WEIGHT", model.fc.weight)
        + fmt_array("FC_BIAS", model.fc.bias)
    )
    header_path.write_text(header_content)
    print(f"Exported C++ weights header to: {header_path.resolve()} ({header_path.stat().st_size} bytes)")
    return 0


def cmd_auto_sort(args: argparse.Namespace) -> int:
    """Use the trained MobileNetV2 model (and reference dataset) to classify and optionally sort a new sampleset."""
    import torch
    import torch.nn as nn
    from PIL import Image, UnidentifiedImageError
    from torchvision import models, transforms

    src_dir = Path(args.source)
    model_path = Path(args.model)
    out_dir = Path(args.out)

    if not src_dir.exists():
        print(f"Error: Source directory {src_dir} does not exist.", file=sys.stderr)
        return 1
    if not model_path.exists():
        print(f"Error: Model file {model_path} does not exist. Run 'train' first.", file=sys.stderr)
        return 1

    device = torch.device("mps" if torch.backends.mps.is_available() else "cpu")

    # Build MobileNetV2 architecture matching cmd_train
    model = models.mobilenet_v2(weights=None)
    in_features = model.classifier[1].in_features
    model.classifier = nn.Sequential(
        nn.Dropout(p=0.2),
        nn.Linear(in_features, 2),
    )
    state_dict = torch.load(model_path, map_location=device, weights_only=True)
    model.load_state_dict(state_dict)
    model.to(device)
    model.eval()

    transform = transforms.Compose([
        transforms.Resize((args.size, args.size)),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ])

    all_files = sorted(list(src_dir.glob("*.jpg")) + list(src_dir.glob("*.jpeg")))
    if not all_files:
        print(f"No JPEG images found in {src_dir}.", file=sys.stderr)
        return 1

    classes = ["healthy", "wilted"]
    min_conf = args.min_conf
    prefix = f"{args.prefix}_" if getattr(args, "prefix", None) else ""
    roi_x0, roi_x1 = parse_roi(args.roi_x)

    results: list[tuple[int, Path, str, float]] = []
    corrupted = 0

    with torch.no_grad():
        for f in all_files:
            idx = extract_index(f.name)
            if idx is None:
                continue
            try:
                with Image.open(f) as img:
                    rgb = crop_canopy_roi(img.convert("RGB"), roi_x0, roi_x1)
            except (UnidentifiedImageError, OSError):
                corrupted += 1
                continue

            x = transform(rgb).unsqueeze(0).to(device)
            logits = model(x)
            probs = torch.softmax(logits, dim=1)[0].cpu()
            pred_idx = int(torch.argmax(probs).item())
            conf = float(probs[pred_idx].item())
            label = classes[pred_idx] if conf >= min_conf else "uncertain"
            results.append((idx, f, label, conf))

    # Summarise contiguous runs so time-series transitions are easy to inspect
    print(f"=== Auto-Sort Analysis for {src_dir} (min_conf={min_conf:.2f}) ===")
    if results:
        runs: list[tuple[int, int, str, int, float]] = []
        cur_start = results[0][0]
        cur_end = results[0][0]
        cur_label = results[0][2]
        cur_count = 1
        cur_conf_sum = results[0][3]

        for idx, _, label, conf in results[1:]:
            if label == cur_label:
                cur_end = idx
                cur_count += 1
                cur_conf_sum += conf
            else:
                runs.append((cur_start, cur_end, cur_label, cur_count, cur_conf_sum / cur_count))
                cur_start = idx
                cur_end = idx
                cur_label = label
                cur_count = 1
                cur_conf_sum = conf
        runs.append((cur_start, cur_end, cur_label, cur_count, cur_conf_sum / cur_count))

        print("\nContiguous time-series segments:")
        for start_i, end_i, lbl, cnt, avg_c in runs:
            span = f"img_{start_i:05d} .. img_{end_i:05d}" if start_i != end_i else f"img_{start_i:05d}          "
            print(f"  {span} | {lbl:<10} | {cnt:3d} photos | avg conf: {avg_c*100:5.1f}%")

    counts = {"healthy": 0, "wilted": 0, "uncertain": 0}
    for _, f, label, _ in results:
        counts[label] += 1
        if args.apply and label in ("healthy", "wilted"):
            dest_dir = out_dir / label
            dest_dir.mkdir(parents=True, exist_ok=True)
            with Image.open(f) as img:
                cropped = crop_canopy_roi(img.convert("RGB"), roi_x0, roi_x1)
                cropped.save(dest_dir / f"{prefix}{f.name}", quality=95)

    print("\nSummary:")
    print(f"  • Healthy   (>= {min_conf*100:.0f}%): {counts['healthy']}")
    print(f"  • Wilted    (>= {min_conf*100:.0f}%): {counts['wilted']}")
    print(f"  • Uncertain (<  {min_conf*100:.0f}%): {counts['uncertain']} (skipped)")
    if corrupted:
        print(f"  • Corrupted / invalid: {corrupted} (skipped)")

    if args.apply:
        print(f"\nCopied {counts['healthy'] + counts['wilted']} high-confidence images into {out_dir} (prefix='{prefix}').")
    else:
        print("\nDry run only. Pass --apply to copy high-confidence images into the output dataset.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Basil Wilt Detection Model Training Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Subcommand: prepare
    prep_p = subparsers.add_parser("prepare", help="Sort photos into healthy and wilted folders by index ranges")
    prep_p.add_argument("--source", default="local/capture/downloads", help="Folder containing raw img_00001.jpg files")
    prep_p.add_argument("--out", default="local/dataset", help="Output dataset root (default: local/dataset)")
    prep_p.add_argument("--healthy", help="Comma-separated ranges for healthy plant, e.g. '50-120'")
    prep_p.add_argument("--wilted", help="Comma-separated ranges for wilted plant, e.g. '1-49,121-179'")
    prep_p.add_argument("--prefix", help="Optional prefix for copied filenames (e.g. 's1' -> 's1_img_00045.jpg')")
    prep_p.add_argument("--roi-x", default="0.20,0.75", help="Horizontal crop fraction 'min,max' (default: '0.20,0.75')")

    # Subcommand: train
    train_p = subparsers.add_parser("train", help="Train a transfer learning classifier locally")
    train_p.add_argument("--data", default="local/dataset", help="Dataset directory containing healthy/ and wilted/")
    train_p.add_argument("--size", type=int, default=96, help="Input resolution (default: 96x96, Edge Impulse compatible)")
    train_p.add_argument("--epochs", type=int, default=12, help="Number of training epochs (default: 12)")
    train_p.add_argument("--batch", type=int, default=16, help="Batch size (default: 16)")
    train_p.add_argument("--lr", type=float, default=0.001, help="Learning rate (default: 0.001)")
    train_p.add_argument("--out", default="local/basil_model.pt", help="Saved model file path (default: local/basil_model.pt)")

    # Subcommand: export-cpp
    exp_p = subparsers.add_parser("export-cpp", help="Train a compact CNN and export C++ weights header for ESP32")
    exp_p.add_argument("--data", default="local/dataset", help="Dataset directory containing healthy/ and wilted/")
    exp_p.add_argument("--epochs", type=int, default=35, help="Number of training epochs (default: 35)")
    exp_p.add_argument("--header", default="src/camera/model_weights.h", help="Output C++ header path")

    # Subcommand: auto-sort
    sort_p = subparsers.add_parser("auto-sort", help="Classify and sort a new sampleset using the trained PyTorch model")
    sort_p.add_argument("--source", required=True, help="Directory of unlabelled images (e.g. local/samplesets/sampleset-3)")
    sort_p.add_argument("--model", default="local/basil_model.pt", help="Trained PyTorch model path (default: local/basil_model.pt)")
    sort_p.add_argument("--out", default="local/dataset", help="Output dataset directory (default: local/dataset)")
    sort_p.add_argument("--size", type=int, default=96, help="Model input resolution (default: 96)")
    sort_p.add_argument("--min-conf", type=float, default=0.80, help="Minimum confidence threshold to auto-assign (default: 0.80)")
    sort_p.add_argument("--prefix", default="s3", help="Prefix for copied filenames (default: 's3')")
    sort_p.add_argument("--roi-x", default="0.20,0.75", help="Horizontal crop fraction 'min,max' (default: '0.20,0.75')")
    sort_p.add_argument("--apply", action="store_true", help="Copy high-confidence images into --out")

    args = parser.parse_args()
    if args.command == "prepare":
        return cmd_prepare(args)
    elif args.command == "train":
        return cmd_train(args)
    elif args.command == "export-cpp":
        return cmd_export_cpp(args)
    elif args.command == "auto-sort":
        return cmd_auto_sort(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
