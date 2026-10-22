#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Dtor->getLocation()))
        return;

    const CXXRecordDecl *Class = Dtor->getParent();
    if (!Class || !Class->isCompleteDefinition())
        return;

    bool HasDerived = false;
    for (const auto *D : Class->getASTContext().getTranslationUnitDecl()->decls()) {
        if (const auto *RD = dyn_cast<CXXRecordDecl>(D)) {
            if (RD != Class && RD->hasDefinition() && RD->isDerivedFrom(Class)) {
                HasDerived = true;
                break;
            }
        }
    }
    if (!HasDerived)
        return;

    SourceLocation TildeLoc = Dtor->getLocation();
    if (TildeLoc.isInvalid())
        return;

    static std::unordered_set<unsigned> Processed;
    if (!Processed.insert(TildeLoc.getRawEncoding()).second)
        return;

    Rewrite.InsertTextBefore(TildeLoc, "virtual ");

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлено virtual к невиртуальному деструктору");
    Diag.Report(Dtor->getLocation(), DiagID);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Method->getLocation()))
        return;

    SourceLocation EndLoc = Method->getNameInfo().getEndLoc();
    if (EndLoc.isInvalid())
        return;

    const char *Buf = SM.getCharacterData(EndLoc);
    if (!Buf)
        return;

    const char *P = Buf;
    int Depth = 0;

    while (*P && *P != '(')
        ++P;
    if (!*P)
        return;

    while (*P) {
        if (*P == '(')
            ++Depth;
        else if (*P == ')') {
            --Depth;
            if (Depth == 0) {
                ++P;
                break;
            }
        }
        ++P;
    }
    if (Depth != 0)
        return;

    SourceLocation AfterParen = EndLoc.getLocWithOffset(P - Buf);

    static std::unordered_set<unsigned> Processed;
    if (!Processed.insert(AfterParen.getRawEncoding()).second)
        return;

    Rewrite.InsertTextAfter(AfterParen, " override");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен override к методу");
    Diag.Report(Method->getLocation(), DiagID);
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(LoopVar->getLocation()))
        return;

    QualType QT = LoopVar->getType().getCanonicalType();
    if (QT->isReferenceType() || QT->isPointerType() || QT->isBuiltinType())
        return;

    auto *TSI = LoopVar->getTypeSourceInfo();
    if (!TSI)
        return;
    TypeLoc TL = TSI->getTypeLoc();
    SourceLocation TypeEndLoc = Lexer::getLocForEndOfToken(TL.getEndLoc(), 0, SM, Rewrite.getLangOpts());

    static std::unordered_set<unsigned> Processed;
    if (!Processed.insert(TypeEndLoc.getRawEncoding()).second)
        return;

    Rewrite.InsertTextBefore(TypeEndLoc, "&");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен & к переменной в цикле");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

auto NvDtorMatcher() {
    return cxxDestructorDecl(unless(isVirtual()), unless(isImplicit()), ofClass(cxxRecordDecl(isDefinition())))
        .bind("nonVirtualDtor");
}

auto NoOverrideMatcher() {
    return cxxMethodDecl(unless(cxxConstructorDecl()), unless(cxxDestructorDecl()), unless(cxxConversionDecl()),
                         unless(hasAttr(attr::Override)), isOverride())
        .bind("missingOverride");
}

auto NoRefConstVarInRangeLoopMatcher() {
    return varDecl(hasType(qualType(isConstQualified(), unless(referenceType()), unless(builtinType()))),
                   hasAncestor(cxxForRangeStmt()))
        .bind("loopVar");
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.

    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}