//===- DependencyScanningTool.cpp - clang-scan-deps service ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "clang/CAS/IncludeTree.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/CAS/CachingOnDiskFileSystem.h"
#include "llvm/CAS/ObjectStore.h"

using namespace clang;
using namespace tooling;
using namespace dependencies;

static std::vector<std::string>
makeTUCommandLineWithoutPaths(ArrayRef<std::string> OriginalCommandLine) {
  std::vector<std::string> Args = OriginalCommandLine;

  Args.push_back("-fno-implicit-modules");
  Args.push_back("-fno-implicit-module-maps");

  // These arguments are unused in explicit compiles.
  llvm::erase_if(Args, [](StringRef Arg) {
    if (Arg.consume_front("-fmodules-")) {
      return Arg.startswith("cache-path=") ||
             Arg.startswith("prune-interval=") ||
             Arg.startswith("prune-after=") ||
             Arg == "validate-once-per-build-session";
    }
    return Arg.startswith("-fbuild-session-file=");
  });

  return Args;
}

DependencyScanningTool::DependencyScanningTool(
    DependencyScanningService &Service,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : Worker(Service, std::move(FS)) {}

llvm::Expected<std::string> DependencyScanningTool::getDependencyFile(
    const std::vector<std::string> &CommandLine, StringRef CWD,
    llvm::Optional<StringRef> ModuleName) {
  /// Prints out all of the gathered dependencies into a string.
  class MakeDependencyPrinterConsumer : public DependencyConsumer {
  public:
    void
    handleDependencyOutputOpts(const DependencyOutputOptions &Opts) override {
      this->Opts = std::make_unique<DependencyOutputOptions>(Opts);
    }

    void handleFileDependency(StringRef File) override {
      Dependencies.push_back(std::string(File));
    }

    void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {
      // Same as `handleModuleDependency`.
    }

    void handleModuleDependency(ModuleDeps MD) override {
      // These are ignored for the make format as it can't support the full
      // set of deps, and handleFileDependency handles enough for implicitly
      // built modules to work.
    }

    void handleContextHash(std::string Hash) override {}

    std::string lookupModuleOutput(const ModuleID &ID,
                                   ModuleOutputKind Kind) override {
      llvm::report_fatal_error("unexpected call to lookupModuleOutput");
    }

    void printDependencies(std::string &S) {
      assert(Opts && "Handled dependency output options.");

      class DependencyPrinter : public DependencyFileGenerator {
      public:
        DependencyPrinter(DependencyOutputOptions &Opts,
                          ArrayRef<std::string> Dependencies)
            : DependencyFileGenerator(Opts) {
          for (const auto &Dep : Dependencies)
            addDependency(Dep);
        }

        void printDependencies(std::string &S) {
          llvm::raw_string_ostream OS(S);
          outputDependencyFile(OS);
        }
      };

      DependencyPrinter Generator(*Opts, Dependencies);
      Generator.printDependencies(S);
    }

  private:
    std::unique_ptr<DependencyOutputOptions> Opts;
    std::vector<std::string> Dependencies;
  };

  MakeDependencyPrinterConsumer Consumer;
  auto Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, ModuleName);
  if (Result)
    return std::move(Result);
  std::string Output;
  Consumer.printDependencies(Output);
  return Output;
}

namespace {
/// Returns a CAS tree containing the dependencies.
class MakeDependencyTree : public DependencyConsumer {
public:
  void handleFileDependency(StringRef File) override {
    // FIXME: Probably we want to delete this class, since we're getting
    // dependencies more accurately (including directories) by intercepting
    // filesystem accesses.
    //
    // On the other hand, for implicitly-discovered modules, we really want to
    // drop a bunch of extra dependencies from the directory iteration.
    //
    // For now just disable this.
    //
    // E = llvm::joinErrors(std::move(E), Builder->push(File));
  }

  void handleModuleDependency(ModuleDeps) override {}
  void handlePrebuiltModuleDependency(PrebuiltModuleDep) override {}
  void handleDependencyOutputOpts(const DependencyOutputOptions &) override {}

  void handleContextHash(std::string) override {}

  std::string lookupModuleOutput(const ModuleID &, ModuleOutputKind) override {
    llvm::report_fatal_error("unexpected call to lookupModuleOutput");
  }

  Expected<llvm::cas::ObjectProxy> makeTree() {
    if (E)
      return std::move(E);
    return Builder->create();
  }

  MakeDependencyTree(llvm::cas::CachingOnDiskFileSystem &FS)
      : E(llvm::Error::success()), Builder(FS.createTreeBuilder()) {}

  ~MakeDependencyTree() {
    // Ignore the error if makeTree wasn't called.
    llvm::consumeError(std::move(E));
  }

private:
  llvm::Error E;
  std::unique_ptr<llvm::cas::CachingOnDiskFileSystem::TreeBuilder> Builder;
};
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTree(
    const std::vector<std::string> &CommandLine, StringRef CWD) {
  llvm::cas::CachingOnDiskFileSystem &FS = Worker.getCASFS();
  FS.trackNewAccesses();
  MakeDependencyTree Consumer(FS);
  auto Result = Worker.computeDependencies(CWD, CommandLine, Consumer);
  if (Result)
    return std::move(Result);
  // return Consumer.makeTree();
  //
  // FIXME: This is needed because the dependency scanner doesn't track
  // directories are accessed -- in particular, we need the CWD to be included.
  // However, if we *want* to filter out certain accesses (such as for modules)
  // this will get in the way.
  //
  // The right fix is to add an API for listing directories that are
  // dependencies, and explicitly add the CWD and other things that matter.
  // (The 'make' output can ignore directories.)
  return FS.createTreeFromNewAccesses();
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTreeFromCompilerInvocation(
    std::shared_ptr<CompilerInvocation> Invocation, StringRef CWD,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation,
    llvm::function_ref<StringRef(const llvm::vfs::CachedDirectoryEntry &)>
        RemapPath) {
  llvm::cas::CachingOnDiskFileSystem &FS = Worker.getCASFS();
  FS.trackNewAccesses();
  FS.setCurrentWorkingDirectory(CWD);
  MakeDependencyTree DepsConsumer(FS);
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, DepsConsumer, DiagsConsumer, VerboseOS,
      DiagGenerationAsCompilation);
  // return DepsConsumer.makeTree();
  //
  // FIXME: See FIXME in getDepencyTree().
  return FS.createTreeFromNewAccesses(RemapPath);
}

namespace {
class IncludeTreePPConsumer : public PPIncludeActionsConsumer {
public:
  explicit IncludeTreePPConsumer(cas::ObjectStore &DB) : DB(DB) {}

  Expected<cas::IncludeTreeRoot> getIncludeTree();

private:
  void enteredInclude(Preprocessor &PP, FileID FID) override;

  void exitedInclude(Preprocessor &PP, FileID IncludedBy, FileID Include,
                     SourceLocation ExitLoc) override;

  void handleHasIncludeCheck(Preprocessor &PP, bool Result) override;

  void finalize(CompilerInstance &CI) override;

  Expected<cas::ObjectRef> getObjectForFile(Preprocessor &PP, FileID FID);
  Expected<cas::ObjectRef>
  getObjectForFileNonCached(FileManager &FM, const SrcMgr::FileInfo &FI);
  Expected<cas::ObjectRef> getObjectForBuffer(const SrcMgr::FileInfo &FI);
  Expected<cas::ObjectRef> addToFileList(FileManager &FM, const FileEntry *FE);

  struct FilePPState {
    SrcMgr::CharacteristicKind FileCharacteristic;
    cas::ObjectRef File;
    SmallVector<std::pair<cas::ObjectRef, uint32_t>, 6> Includes;
    llvm::SmallBitVector HasIncludeChecks;
  };

  Expected<cas::IncludeTree> getCASTreeForFileIncludes(FilePPState &&PPState);

  bool hasErrorOccurred() const { return ErrorToReport.hasValue(); }

  template <typename T> Optional<T> check(Expected<T> &&E) {
    if (!E) {
      ErrorToReport = E.takeError();
      return None;
    }
    return *E;
  }

  cas::ObjectStore &DB;
  Optional<cas::ObjectRef> PCHRef;
  llvm::BitVector SeenIncludeFiles;
  SmallVector<cas::IncludeFileList::FileEntry> IncludedFiles;
  Optional<cas::ObjectRef> PredefinesBufferRef;
  SmallVector<FilePPState> IncludeStack;
  llvm::DenseMap<const FileEntry *, Optional<cas::ObjectRef>> ObjectForFile;
  Optional<llvm::Error> ErrorToReport;
};
} // namespace

void IncludeTreePPConsumer::enteredInclude(Preprocessor &PP, FileID FID) {
  if (hasErrorOccurred())
    return;

  Optional<cas::ObjectRef> FileRef = check(getObjectForFile(PP, FID));
  if (!FileRef)
    return;
  const SrcMgr::FileInfo &FI =
      PP.getSourceManager().getSLocEntry(FID).getFile();
  IncludeStack.push_back({FI.getFileCharacteristic(), *FileRef, {}, {}});
}

void IncludeTreePPConsumer::exitedInclude(Preprocessor &PP, FileID IncludedBy,
                                          FileID Include,
                                          SourceLocation ExitLoc) {
  if (hasErrorOccurred())
    return;

  assert(*check(getObjectForFile(PP, Include)) == IncludeStack.back().File);
  Optional<cas::IncludeTree> IncludeTree =
      check(getCASTreeForFileIncludes(IncludeStack.pop_back_val()));
  if (!IncludeTree)
    return;
  assert(*check(getObjectForFile(PP, IncludedBy)) == IncludeStack.back().File);
  SourceManager &SM = PP.getSourceManager();
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedExpansionLoc(ExitLoc);
  IncludeStack.back().Includes.push_back(
      {IncludeTree->getRef(), LocInfo.second});
}

void IncludeTreePPConsumer::handleHasIncludeCheck(Preprocessor &PP,
                                                  bool Result) {
  if (hasErrorOccurred())
    return;

  IncludeStack.back().HasIncludeChecks.push_back(Result);
}

void IncludeTreePPConsumer::finalize(CompilerInstance &CI) {
  FileManager &FM = CI.getFileManager();

  auto addFile = [&](StringRef FilePath, bool IgnoreFileError = false) -> bool {
    llvm::ErrorOr<const FileEntry *> FE = FM.getFile(FilePath);
    if (!FE) {
      if (IgnoreFileError)
        return true;
      ErrorToReport = llvm::errorCodeToError(FE.getError());
      return false;
    }
    Expected<cas::ObjectRef> Ref = addToFileList(FM, *FE);
    if (!Ref) {
      ErrorToReport = Ref.takeError();
      return false;
    }
    return true;
  };

  for (StringRef FilePath : CI.getLangOpts().NoSanitizeFiles) {
    if (!addFile(FilePath))
      return;
  }

  StringRef Sysroot = CI.getHeaderSearchOpts().Sysroot;
  if (!Sysroot.empty()) {
    // Include 'SDKSettings.json', if it exists, to accomodate availability
    // checks during the compilation.
    llvm::SmallString<256> FilePath = Sysroot;
    llvm::sys::path::append(FilePath, "SDKSettings.json");
    addFile(FilePath, /*IgnoreFileError*/ true);
  }

  PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  if (PPOpts.ImplicitPCHInclude.empty())
    return; // no need for additional work.

  // Go through all the recorded included files; we'll get additional files from
  // the PCH that we need to include in the file list, in case they are
  // referenced while replaying the include-tree.
  SmallVector<const FileEntry *, 32> NotSeenIncludes;
  for (const FileEntry *FE : CI.getPreprocessor().getIncludedFiles()) {
    if (FE->getUID() >= SeenIncludeFiles.size() ||
        !SeenIncludeFiles[FE->getUID()])
      NotSeenIncludes.push_back(FE);
  }
  // Sort so we can visit the files in deterministic order.
  llvm::sort(NotSeenIncludes, [](const FileEntry *LHS, const FileEntry *RHS) {
    return LHS->getUID() < RHS->getUID();
  });

  for (const FileEntry *FE : NotSeenIncludes) {
    auto FileNode = addToFileList(FM, FE);
    if (!FileNode) {
      ErrorToReport = FileNode.takeError();
      return;
    }
  }

  llvm::ErrorOr<Optional<cas::ObjectRef>> CASContents =
      FM.getObjectRefForFileContent(PPOpts.ImplicitPCHInclude);
  if (!CASContents) {
    ErrorToReport = llvm::errorCodeToError(CASContents.getError());
    return;
  }
  PCHRef = **CASContents;
}

Expected<cas::ObjectRef>
IncludeTreePPConsumer::getObjectForFile(Preprocessor &PP, FileID FID) {
  SourceManager &SM = PP.getSourceManager();
  const SrcMgr::FileInfo &FI = SM.getSLocEntry(FID).getFile();
  if (PP.getPredefinesFileID() == FID) {
    if (!PredefinesBufferRef) {
      auto Ref = getObjectForBuffer(FI);
      if (!Ref)
        return Ref.takeError();
      PredefinesBufferRef = *Ref;
    }
    return *PredefinesBufferRef;
  }
  assert(FI.getContentCache().OrigEntry);
  auto &FileRef = ObjectForFile[FI.getContentCache().OrigEntry];
  if (!FileRef.hasValue()) {
    auto Ref = getObjectForFileNonCached(SM.getFileManager(), FI);
    if (!Ref)
      return Ref.takeError();
    FileRef = *Ref;
  }
  return FileRef.getValue();
}

Expected<cas::ObjectRef>
IncludeTreePPConsumer::getObjectForFileNonCached(FileManager &FM,
                                                 const SrcMgr::FileInfo &FI) {
  const FileEntry *FE = FI.getContentCache().OrigEntry;
  assert(FE);

  // Mark the include as already seen.
  if (FE->getUID() >= SeenIncludeFiles.size())
    SeenIncludeFiles.resize(FE->getUID() + 1);
  SeenIncludeFiles.set(FE->getUID());

  return addToFileList(FM, FE);
}

Expected<cas::ObjectRef>
IncludeTreePPConsumer::getObjectForBuffer(const SrcMgr::FileInfo &FI) {
  // This is a non-file buffer, like the predefines.
  auto Ref = DB.storeFromString(
      {}, FI.getContentCache().getBufferIfLoaded()->getBuffer());
  if (!Ref)
    return Ref.takeError();
  Expected<cas::IncludeFile> FileNode =
      cas::IncludeFile::create(DB, FI.getName(), *Ref);
  if (!FileNode)
    return FileNode.takeError();
  return FileNode->getRef();
}

Expected<cas::ObjectRef>
IncludeTreePPConsumer::addToFileList(FileManager &FM, const FileEntry *FE) {
  StringRef Filename = FE->getName();
  llvm::ErrorOr<Optional<cas::ObjectRef>> CASContents =
      FM.getObjectRefForFileContent(Filename);
  if (!CASContents)
    return llvm::errorCodeToError(CASContents.getError());
  assert(*CASContents);

  auto addFile = [&](StringRef Filename) -> Expected<cas::ObjectRef> {
    assert(!Filename.empty());
    auto FileNode = cas::IncludeFile::create(DB, Filename, **CASContents);
    if (!FileNode)
      return FileNode.takeError();
    IncludedFiles.push_back(
        {FileNode->getRef(),
         static_cast<cas::IncludeFileList::FileSizeTy>(FE->getSize())});
    return FileNode->getRef();
  };

  StringRef OtherPath = FE->tryGetRealPathName();
  if (!OtherPath.empty()) {
    // Check whether another path is associated due to a symlink.
    llvm::SmallString<128> AbsPath(Filename);
    FM.makeAbsolutePath(AbsPath);
    llvm::sys::path::remove_dots(AbsPath, /*remove_dot_dot=*/true);
    if (OtherPath != AbsPath) {
      auto FileNode = addFile(OtherPath);
      if (!FileNode)
        return FileNode.takeError();
    }
  }

  return addFile(Filename);
}

Expected<cas::IncludeTree>
IncludeTreePPConsumer::getCASTreeForFileIncludes(FilePPState &&PPState) {
  return cas::IncludeTree::create(DB, PPState.FileCharacteristic, PPState.File,
                                  PPState.Includes, PPState.HasIncludeChecks);
}

Expected<cas::IncludeTreeRoot> IncludeTreePPConsumer::getIncludeTree() {
  if (ErrorToReport)
    return std::move(*ErrorToReport);

  assert(IncludeStack.size() == 1);
  Expected<cas::IncludeTree> MainIncludeTree =
      getCASTreeForFileIncludes(IncludeStack.pop_back_val());
  if (!MainIncludeTree)
    return MainIncludeTree.takeError();
  auto FileList = cas::IncludeFileList::create(DB, IncludedFiles);
  if (!FileList)
    return FileList.takeError();

  return cas::IncludeTreeRoot::create(DB, MainIncludeTree->getRef(),
                                      FileList->getRef(), PCHRef);
}

Expected<cas::IncludeTreeRoot> DependencyScanningTool::getIncludeTree(
    cas::ObjectStore &DB, const std::vector<std::string> &CommandLine,
    StringRef CWD) {
  IncludeTreePPConsumer Consumer(DB);
  llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer);
  if (Result)
    return std::move(Result);
  return Consumer.getIncludeTree();
}

Expected<cas::IncludeTreeRoot>
DependencyScanningTool::getIncludeTreeFromCompilerInvocation(
    cas::ObjectStore &DB, std::shared_ptr<CompilerInvocation> Invocation,
    StringRef CWD, DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  IncludeTreePPConsumer Consumer(DB);
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, Consumer, DiagsConsumer, VerboseOS,
      DiagGenerationAsCompilation);
  return Consumer.getIncludeTree();
}

llvm::Expected<FullDependenciesResult>
DependencyScanningTool::getFullDependencies(
    const std::vector<std::string> &CommandLine, StringRef CWD,
    const llvm::StringSet<> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput,
    llvm::Optional<StringRef> ModuleName) {
  FullDependencyConsumer Consumer(AlreadySeen, LookupModuleOutput);
  llvm::cas::CachingOnDiskFileSystem *FS =
    Worker.useCAS() ? &Worker.getCASFS() : nullptr;
  if (FS) {
    FS->trackNewAccesses();
    FS->setCurrentWorkingDirectory(CWD);
  }
  llvm::Error Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, ModuleName);
  if (Result)
    return std::move(Result);

  Optional<cas::CASID> CASFileSystemRootID;
  if (FS) {
    if (auto Tree = FS->createTreeFromNewAccesses())
      CASFileSystemRootID = Tree->getID();
    else
      return Tree.takeError();
  }

  return Consumer.getFullDependencies(CommandLine, CASFileSystemRootID);
}

FullDependenciesResult FullDependencyConsumer::getFullDependencies(
    const std::vector<std::string> &OriginalCommandLine,
    Optional<cas::CASID> CASFileSystemRootID) const {
  FullDependencies FD;

  FD.CommandLine = makeTUCommandLineWithoutPaths(
      ArrayRef<std::string>(OriginalCommandLine).slice(1));

  FD.ID.ContextHash = std::move(ContextHash);

  FD.FileDeps.assign(Dependencies.begin(), Dependencies.end());

  for (const PrebuiltModuleDep &PMD : PrebuiltModuleDeps)
    FD.CommandLine.push_back("-fmodule-file=" + PMD.PCMFile);

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    if (MD.ImportedByMainFile) {
      FD.ClangModuleDeps.push_back(MD.ID);
      FD.CommandLine.push_back(
          "-fmodule-file=" +
          LookupModuleOutput(MD.ID, ModuleOutputKind::ModuleFile));
    }
  }

  FD.PrebuiltModuleDeps = std::move(PrebuiltModuleDeps);

  FD.CASFileSystemRootID = CASFileSystemRootID;

  FullDependenciesResult FDR;

  for (auto &&M : ClangModuleDeps) {
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    FDR.DiscoveredModules.push_back(std::move(M.second));
  }

  FDR.FullDeps = std::move(FD);
  return FDR;
}
