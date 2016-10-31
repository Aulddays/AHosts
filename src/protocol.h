// dns protocol constants
#pragma once

enum QuestionType
{
	QT_A = 1,
	QT_CNAME = 5,
	QT_AAAA = 28,
};

enum QuestionClass
{
	QC_IN = 1
};